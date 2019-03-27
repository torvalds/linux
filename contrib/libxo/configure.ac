#
# $Id$
#
# See ./INSTALL for more info
#

#
# Release numbering: even numbered dot releases are official ones, and
# odd numbers are development ones.  The svn version of this file will
# only (ONLY!) ever (EVER!) contain odd numbers, so I'll always know if
# a particular user has the dist or svn release.
#

AC_PREREQ(2.2)
AC_INIT([libxo], [0.9.0], [phil@juniper.net])
AM_INIT_AUTOMAKE([-Wall -Werror foreign -Wno-portability])

# Support silent build rules.  Requires at least automake-1.11.
# Disable with "configure --disable-silent-rules" or "make V=1"
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_INSTALL
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_LN_S

# Must be after AC_PROG_AR
LT_INIT([dlopen shared])

AC_PATH_PROG(BASENAME, basename, /usr/bin/basename)
AC_PATH_PROG(BISON, bison, /usr/bin/bison)
AC_PATH_PROG(CAT, cat, /bin/cat)
AC_PATH_PROG(CHMOD, chmod, /bin/chmod)
AC_PATH_PROG(CP, cp, /bin/cp)
AC_PATH_PROG(DIFF, diff, /usr/bin/diff)
AC_PATH_PROG(MKDIR, mkdir, /bin/mkdir)
AC_PATH_PROG(MV, mv, /bin/mv)
AC_PATH_PROG(RM, rm, /bin/rm)
AC_PATH_PROG(SED, sed, /bin/sed)

AC_STDC_HEADERS

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([bzero memmove strchr strcspn strerror strspn])
AC_CHECK_FUNCS([sranddev srand strlcpy])
AC_CHECK_FUNCS([fdopen getrusage])
AC_CHECK_FUNCS([gettimeofday ctime])
AC_CHECK_FUNCS([getpass])
AC_CHECK_FUNCS([getprogname])
AC_CHECK_FUNCS([sysctlbyname])
AC_CHECK_FUNCS([flock])
AC_CHECK_FUNCS([asprintf])
AC_CHECK_FUNCS([__flbf])
AC_CHECK_FUNCS([sysctlbyname])


AC_CHECK_HEADERS([dlfcn.h])
AC_CHECK_HEADERS([dlfcn.h])
AC_CHECK_HEADERS([stdio_ext.h])
AC_CHECK_HEADERS([tzfile.h])
AC_CHECK_HEADERS([stdtime/tzfile.h])
AC_CHECK_FUNCS([dlfunc])

AC_CHECK_HEADERS([sys/time.h])
AC_CHECK_HEADERS([ctype.h errno.h stdio.h stdlib.h])
AC_CHECK_HEADERS([string.h sys/param.h unistd.h ])
AC_CHECK_HEADERS([sys/sysctl.h])
AC_CHECK_HEADERS([threads.h])
AC_CHECK_HEADERS([monitor.h])

dnl humanize_number(3) is a great function, but it's not standard.
dnl Note Macosx has the function in libutil.a but doesn't ship the
dnl header file, so I'll need to carry my own implementation.  See:
dnl    https://devforums.apple.com/thread/271121
AC_CHECK_HEADERS([libutil.h])
AC_CHECK_LIB([util], [humanize_number],
     [HAVE_HUMANIZE_NUMBER=$ac_cv_header_libutil_h],
     [HAVE_HUMANIZE_NUMBER=no])

AC_MSG_RESULT(humanize_number results: :${HAVE_HUMANIZE_NUMBER}:${ac_cv_header_libutil_h}:)

if test "$HAVE_HUMANIZE_NUMBER" = "yes"; then
    AC_DEFINE([HAVE_HUMANIZE_NUMBER], [1], [humanize_number(3)])
fi

AM_CONDITIONAL([HAVE_HUMANIZE_NUMBER], [test "$HAVE_HUMANIZE_NUMBER" = "yes"])

AC_ARG_ENABLE([gettext],
    [  --disable-gettext  Turn off support for gettext],
    [GETTEXT_ENABLE=$enableval],
    [GETTEXT_ENABLE=yes])

dnl Looking for gettext(), assumably in libintl
AC_ARG_WITH(gettext,
        [  --with-gettext=[PFX]           Specify location of gettext installation],
        [GETTEXT_PREFIX=$withval],
	[GETTEXT_PREFIX=/usr],
)

HAVE_GETTEXT=no

if test "$GETTEXT_ENABLE" != "no"; then

  AC_MSG_CHECKING([gettext in ${GETTEXT_PREFIX}])

  _save_cflags="$CFLAGS"
  CFLAGS="$CFLAGS -I${GETTEXT_PREFIX}/include -L${GETTEXT_PREFIX}/lib -Werror -lintl"
  AC_LINK_IFELSE([AC_LANG_SOURCE([[#include <libintl.h>]
             [int main() {char *cp = dgettext(NULL, "xx"); return 0; }]])],
             [HAVE_GETTEXT=yes],
             [HAVE_GETTEXT=no])
  CFLAGS="$_save_cflags"

  AC_MSG_RESULT([$HAVE_GETTEXT])

  if test "$HAVE_GETTEXT" != "yes"; then
      GETTEXT_PREFIX=/opt/local
      AC_MSG_CHECKING([gettext in ${GETTEXT_PREFIX}])

      _save_cflags="$CFLAGS"
      CFLAGS="$CFLAGS -I${GETTEXT_PREFIX}/include -L${GETTEXT_PREFIX}/lib -Werror -lintl"
      AC_LINK_IFELSE([AC_LANG_SOURCE([[#include <libintl.h>]
                 [int main() {char *cp = dgettext(NULL, "xx"); return 0; }]])],
                 [HAVE_GETTEXT=yes],
                 [HAVE_GETTEXT=no])
      CFLAGS="$_save_cflags"

      AC_MSG_RESULT([$HAVE_GETTEXT])
  fi

  if test "$HAVE_GETTEXT" != "yes"; then
      GETTEXT_PREFIX=/usr/local
      AC_MSG_CHECKING([gettext in ${GETTEXT_PREFIX}])

      _save_cflags="$CFLAGS"
      CFLAGS="$CFLAGS -I${GETTEXT_PREFIX}/include -L${GETTEXT_PREFIX}/lib -Werror -lintl"
      AC_LINK_IFELSE([AC_LANG_SOURCE([[#include <libintl.h>]
                 [int main() {char *cp = dgettext(NULL, "xx"); return 0; }]])],
                 [HAVE_GETTEXT=yes],
                 [HAVE_GETTEXT=no])
      CFLAGS="$_save_cflags"

      AC_MSG_RESULT([$HAVE_GETTEXT])
  fi
fi

if test "$HAVE_GETTEXT" = "yes"; then
    AC_DEFINE([HAVE_GETTEXT], [1], [gettext(3)])
    GETTEXT_CFLAGS="-I${GETTEXT_PREFIX}/include"
    GETTEXT_LIBS="-L${GETTEXT_PREFIX}/lib -lintl"
else
    GETTEXT_PREFIX=none
    GETTEXT_CFLAGS=
    GETTEXT_LIBS=
fi
AC_SUBST(GETTEXT_CFLAGS)
AC_SUBST(GETTEXT_LIBS)

GETTEXT_LIBDIR=${GETTEXT_PREFIX}/lib
AC_SUBST(GETTEXT_LIBDIR)
if test -x ${GETTEXT_PREFIX}/bin/msgfmt ; then
    GETTEXT_BINDIR=${GETTEXT_PREFIX}/bin
elif test -x ${GETTEXT_PREFIX}/local/bin/msgfmt ; then
    GETTEXT_BINDIR=${GETTEXT_PREFIX}/local/bin
else
    AC_MSG_NOTICE("could not find msgfmt tool")
    # Use a (bad) fall back value
    GETTEXT_BINDIR=${GETTEXT_PREFIX}/bin
fi
AC_SUBST(GETTEXT_BINDIR)

AM_CONDITIONAL([HAVE_GETTEXT], [test "$HAVE_GETTEXT" = "yes"])

dnl Looking for how to do thread-local variables
AC_ARG_WITH(threads,
        [  --with-threads=[STYLE]           Specify style of thread-local support (none)],
        [THREAD_LOCAL=$withval],
	[THREAD_LOCAL=unknown],
)

AC_MSG_CHECKING([thread-locals are ${THREAD_LOCAL}])

if test "$THREAD_LOCAL" = "unknown"; then
     AC_LINK_IFELSE([AC_LANG_SOURCE([[]
           [__thread int foo; int main() { foo++; return foo; }]])],
           [THREAD_LOCAL=before],
           [THREAD_LOCAL=unknown])

    AC_MSG_RESULT([$THREAD_LOCAL])
fi

if test "$THREAD_LOCAL" = "unknown"; then
    AC_LINK_IFELSE([AC_LANG_SOURCE([[]
           [int __thread foo; int main() { foo++; return foo; }]])],
           [THREAD_LOCAL=after],
           [THREAD_LOCAL=unknown])
    AC_MSG_RESULT([$THREAD_LOCAL])
fi

if test "$THREAD_LOCAL" = "unknown"; then
    AC_LINK_IFELSE([AC_LANG_SOURCE([[]
           [__declspec(int) foo; int main() { foo++; return foo; }]])],
           [THREAD_LOCAL=declspec],
           [THREAD_LOCAL=unknown])
    AC_MSG_RESULT([$THREAD_LOCAL])
fi

if test "$THREAD_LOCAL" != "unknown"; then
    AC_DEFINE_UNQUOTED([HAVE_THREAD_LOCAL], 
              THREAD_LOCAL_${THREAD_LOCAL}, [thread-local setting])
fi

dnl Looking for libcrypto....
AC_CHECK_LIB([crypto], [MD5_Init])
AM_CONDITIONAL([HAVE_LIBCRYPTO], [test "$HAVE_LIBCRYPTO" != "no"])

AC_CHECK_MEMBER([struct sockaddr_un.sun_len],
    [HAVE_SUN_LEN=yes ;
        AC_DEFINE([HAVE_SUN_LEN], [1], [Have struct sockaddr_un.sun_len])],
    [HAS_SUN_LEN=no], [[#include <sys/un.h>]])

AC_CHECK_DECLS([__isthreaded], [], [], [#include <stdio.h>])
HAVE_ISTHREADED=${ac_cv_have_decl___isthreaded}

dnl
dnl Some packages need to be checked against version numbers so we
dnl define a function here for later use
dnl
AC_DEFUN([VERSION_TO_NUMBER],
[`$1 | sed -e 's/lib.* //' | awk 'BEGIN { FS = "."; } { printf "%d", ([$]1 * 1000 + [$]2) * 1000 + [$]3;}'`])

LIBSLAX_CONFIG_PREFIX=""
LIBSLAX_SRC=""

AC_ARG_WITH(libslax-prefix,
        [  --with-libslax-prefix=[PFX]           Specify location of libslax config],
        LIBSLAX_CONFIG_PREFIX=$withval
)

AC_MSG_CHECKING(for libslax) 
if test "x$LIBSLAX_CONFIG_PREFIX" != "x"
then
        SLAX_CONFIG=${LIBSLAX_CONFIG_PREFIX}/bin/slax-config
else
        SLAX_CONFIG=slax-config
fi

dnl
dnl make sure slax-config is executable,
dnl test version and init our variables
dnl

if ${SLAX_CONFIG} --libs > /dev/null 2>&1
then
	LIBSLAX_VERSION=`$SLAX_CONFIG --version`
	SLAX_BINDIR="`$SLAX_CONFIG --bindir | head -1`"
	SLAX_OXTRADOCDIR="`$SLAX_CONFIG --oxtradoc | head -1`"
	AC_MSG_RESULT($LIBSLAX_VERSION found)
	HAVE_OXTRADOC=yes
else
	LIBSLAX_VERSION=
	SLAX_BINDIR=
	SLAX_OXTRADOCDIR=
	AC_MSG_RESULT([no])
	HAVE_OXTRADOC=no
fi
AM_CONDITIONAL([HAVE_OXTRADOC], [test "$HAVE_OXTRADOC" != "no"])

AC_SUBST(SLAX_BINDIR)
AC_SUBST(SLAX_OXTRADOCDIR)

AC_MSG_CHECKING([whether to build with warnings])
AC_ARG_ENABLE([warnings],
    [  --enable-warnings    Turn on compiler warnings],
    [LIBXO_WARNINGS=$enableval],
    [LIBXO_WARNINGS=no])
AC_MSG_RESULT([$LIBXO_WARNINGS])
AM_CONDITIONAL([LIBXO_WARNINGS_HIGH], [test "$LIBXO_WARNINGS" != "no"])

AC_MSG_CHECKING([whether to build with debugging])
AC_ARG_ENABLE([debug],
    [  --enable-debug    Turn on debugging],
    [LIBXO_DEBUG=yes; AC_DEFINE([LIBXO_DEBUG], [1], [Enable debugging])],
    [LIBXO_DEBUG=no])
AC_MSG_RESULT([$LIBXO_DEBUG])
AM_CONDITIONAL([LIBXO_DEBUG], [test "$LIBXO_DEBUG" != "no"])

AC_MSG_CHECKING([whether to use int return codes])
AC_ARG_ENABLE([int-return-codes],
    [  --enable-int-return-codes    Use int return codes (instead of ssize_t)],
    [USE_INT_RETURN_CODES=yes; AC_DEFINE([USE_INT_RETURN_CODES], [1], [Use int return codes])],
    [USE_INT_RETURN_CODES=no])
AC_MSG_RESULT([$USE_INT_RETURN_CODES])

AC_MSG_CHECKING([whether to build with text-only rendering])
AC_ARG_ENABLE([text-only],
    [  --enable-text-only    Turn on text-only rendering],
    [LIBXO_TEXT_ONLY=yes; AC_DEFINE([LIBXO_TEXT_ONLY], [1], [Enable text-only rendering])],
    [LIBXO_TEXT_ONLY=no])
AC_MSG_RESULT([$LIBXO_TEXT_ONLY])
AM_CONDITIONAL([LIBXO_TEXT_ONLY], [test "$LIBXO_TEXT_ONLY" != "no"])

AC_MSG_CHECKING([whether to build with local wcwidth implementation])
AC_ARG_ENABLE([wcwidth],
    [  --disable-wcwidth        Disable local wcwidth implementation],
    [LIBXO_WCWIDTH=$enableval],
    [LIBXO_WCWIDTH=yes])
AC_MSG_RESULT([$LIBXO_WCWIDTH])
if test "${LIBXO_WCWIDTH}" != "no"; then
  AC_DEFINE([LIBXO_WCWIDTH], [1], [Enable local wcwidth implementation])
fi

AC_MSG_CHECKING([retain hash bucket size])
AC_ARG_WITH(retain-size,
        [  --with-retain-size=[DIR]           Specify retain hash bucket size (in bits)],
        [XO_RETAIN_SIZE=$withval],
        [XO_RETAIN_SIZE=default]
)

AC_MSG_RESULT([$XO_RETAIN_SIZE])
if test "${XO_RETAIN_SIZE}" != "default"; then
  AC_DEFINE_UNQUOTED([XO_RETAIN_SIZE], ${XO_RETAIN_SIZE}, [Retain hash bucket size])
fi

AC_CHECK_LIB([m], [lrint])
AM_CONDITIONAL([HAVE_LIBM], [test "$HAVE_LIBM" != "no"])

AC_MSG_CHECKING([compiler for gcc])
HAVE_GCC=no
if test "${CC}" != ""; then
    HAVE_GCC=`${CC} --version 2>&1 | grep GCC`
    if test "${HAVE_GCC}" != ""; then
        HAVE_GCC=yes
    else
        HAVE_GCC=no
    fi
fi
AC_MSG_RESULT([$HAVE_GCC])
AM_CONDITIONAL([HAVE_GCC], [test "$HAVE_GCC" = "yes"])

AC_MSG_CHECKING([whether to build with printflike])
AC_ARG_ENABLE([printflike],
    [  --enable-printflike    Enable use of GCC __printflike attribute],
    [HAVE_PRINTFLIKE=yes;
           AC_DEFINE([HAVE_PRINTFLIKE], [1], [Support printflike])],
    [HAVE_PRINTFLIKE=no])
AC_MSG_RESULT([$HAVE_PRINTFLIKE])
AM_CONDITIONAL([HAVE_PRINTFLIKE], [test "$HAVE_PRINTFLIKE" != ""])

AC_MSG_CHECKING([whether to build with LIBXO_OPTIONS])
AC_ARG_ENABLE([libxo-options],
    [  --disable-libxo-options  Turn off support for LIBXO_OPTIONS],
    [LIBXO_OPTS=$enableval],
    [LIBXO_OPTS=yes])
AC_MSG_RESULT([$LIBXO_OPTS])
AM_CONDITIONAL([NO_LIBXO_OPTIONS], [test "$LIBXO_OPTS" != "yes"])

case $host_os in
     darwin*)
        LIBTOOL=glibtool
	XO_LIBEXT=dylib
	;;
     Linux*|linux*)
        CFLAGS="-D_GNU_SOURCE $CFLAGS"
	LDFLAGS=-ldl
	XO_LIBEXT=so
	;;
     cygwin*|CYGWIN*)
	LDFLAGS=-no-undefined
	XO_LIBEXT=ddl
	;;
esac

case $prefix in
     NONE)
	prefix=/usr/local
	;;
esac

XO_LIBS=-lxo
XO_SRCDIR=${srcdir}
XO_LIBDIR=${libdir}
XO_BINDIR=${bindir}
XO_INCLUDEDIR=${includedir}
XO_CFLAGS="${CFLAGS}"

AC_SUBST(XO_LIBS)
AC_SUBST(XO_SRCDIR)
AC_SUBST(XO_LIBDIR)
AC_SUBST(XO_BINDIR)
AC_SUBST(XO_INCLUDEDIR)
AC_SUBST(XO_LIBEXT)
AC_SUBST(XO_CFLAGS)

AC_ARG_WITH(encoder-dir,
        [  --with-encoder-dir=[DIR]           Specify location of encoder libraries],
        [XO_ENCODERDIR=$withval],
        [XO_ENCODERDIR=$libdir/libxo/encoder]
)
AC_SUBST(XO_ENCODERDIR)

AC_ARG_WITH(share-dir,
        [  --with-share-dir=[DIR]           Specify location of shared files],
        [XO_SHAREDIR=$withval],
        [XO_SHAREDIR=$datarootdir/libxo]
)
XO_SHAREDIR=`echo $XO_SHAREDIR | sed "s;\\${prefix};$prefix;"`
AC_SUBST(XO_SHAREDIR)

dnl for the spec file
RELDATE=`date +'%Y-%m-%d%n'`
AC_SUBST(RELDATE)

AC_MSG_RESULT(Using configure dir $ac_abs_confdir)

if test -d $ac_abs_confdir/.git ; then
  extra=`git branch | awk '/\*/ { print $2 }'`
  if test "$extra" != "" -a "$extra" != "master"
  then
      LIBXO_VERSION_EXTRA="-git-$extra"
  fi
fi

LIBXO_VERSION=$PACKAGE_VERSION
LIBXO_VERSION_NUMBER=VERSION_TO_NUMBER(echo $PACKAGE_VERSION)
AC_SUBST(LIBXO_VERSION)
AC_SUBST(LIBXO_VERSION_NUMBER)
AC_SUBST(LIBXO_VERSION_EXTRA)

AC_DEFINE_UNQUOTED(LIBXO_VERSION, ["$LIBXO_VERSION"],
    [Version number as dotted value])
AC_DEFINE_UNQUOTED(LIBXO_VERSION_NUMBER, [$LIBXO_VERSION_NUMBER],
    [Version number as a number])
AC_DEFINE_UNQUOTED(LIBXO_VERSION_STRING, ["$LIBXO_VERSION_NUMBER"],
    [Version number as string])
AC_DEFINE_UNQUOTED(LIBXO_VERSION_EXTRA, ["$LIBXO_VERSION_EXTRA"],
    [Version number extra information])

AC_CONFIG_HEADERS([libxo/xo_config.h])
AC_CONFIG_FILES([
  Makefile
  libxo-config
  xohtml/xohtml.sh
  libxo/Makefile
  libxo/add.man
  encoder/Makefile
  encoder/cbor/Makefile
  encoder/test/Makefile
  xo/Makefile
  xolint/Makefile
  xohtml/Makefile
  xopo/Makefile
  packaging/libxo.pc
  doc/Makefile
  tests/Makefile
  tests/core/Makefile
  tests/gettext/Makefile
  tests/xo/Makefile
  packaging/libxo.spec
  packaging/libxo.rb.base
])
AC_OUTPUT

AC_MSG_NOTICE([summary of build options:

  libxo version:    ${VERSION} ${LIBXO_VERSION_EXTRA}
  host type:        ${host} / ${host_os}
  install prefix:   ${prefix}
  srcdir:           ${XO_SRCDIR}
  libdir:           ${XO_LIBDIR}
  bindir:           ${XO_BINDIR}
  includedir:       ${XO_INCLUDEDIR}
  share dir:        ${XO_SHAREDIR}
  extensions dir:   ${XO_ENCODERDIR}
  oxtradoc dir:     ${SLAX_OXTRADOCDIR}

  compiler:         ${CC} (${HAVE_GCC:-no})
  compiler flags:   ${CFLAGS}
  library types:    Shared=${enable_shared}, Static=${enable_static}

  warnings:         ${LIBXO_WARNINGS:-no}
  debug:            ${LIBXO_DEBUG:-no}
  printf-like:      ${HAVE_PRINTFLIKE:-no}
  libxo-options:    ${LIBXO_OPTS:-no}
  text-only:        ${LIBXO_TEXT_ONLY:-no}
  gettext:          ${HAVE_GETTEXT:-no} (${GETTEXT_PREFIX})
  isthreaded:       ${HAVE_ISTHREADED:-no}
  thread-local:     ${THREAD_LOCAL:-no}
  local wcwidth:    ${LIBXO_WCWIDTH:-no}
  retain size:      ${XO_RETAIN_SIZE:-no}
])
