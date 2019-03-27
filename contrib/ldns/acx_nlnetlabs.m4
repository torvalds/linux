# acx_nlnetlabs.m4 - common macros for configure checks
# Copyright 2009, Wouter Wijngaards, NLnet Labs.   
# BSD licensed.
#
# Version 34
# 2016-03-21 Check -ldl -pthread for libcrypto for ldns and openssl 1.1.0.
# 2016-03-21 Use HMAC_Update instead of HMAC_CTX_Init (for openssl-1.1.0).
# 2016-01-04 -D_DEFAULT_SOURCE defined with -D_BSD_SOURCE for Linux glibc 2.20
# 2015-12-11 FLTO check for new OSX, clang.
# 2015-11-18 spelling check fix.
# 2015-11-05 ACX_SSL_CHECKS no longer adds -ldl needlessly.
# 2015-08-28 ACX_CHECK_PIE and ACX_CHECK_RELRO_NOW added.
# 2015-03-17 AHX_CONFIG_REALLOCARRAY added
# 2013-09-19 FLTO help text improved.
# 2013-07-18 Enable ACX_CHECK_COMPILER_FLAG to test for -Wstrict-prototypes
# 2013-06-25 FLTO has --disable-flto option.
# 2013-05-03 Update W32_SLEEP for newer mingw that links but not defines it.
# 2013-03-22 Fix ACX_RSRC_VERSION for long version numbers.
# 2012-02-09 Fix AHX_MEMCMP_BROKEN with undef in compat/memcmp.h.
# 2012-01-20 Fix COMPILER_FLAGS_UNBOUND for gcc 4.6.2 assigned-not-used-warns.
# 2011-12-05 Fix getaddrinfowithincludes on windows with fedora16 mingw32-gcc.
# 	     Fix ACX_MALLOC for redefined malloc error.
# 	     Fix GETADDRINFO_WITH_INCLUDES to add -lws2_32
# 2011-11-10 Fix FLTO test to not drop a.out in current directory.
# 2011-11-01 Fix FLTO test for llvm on Lion.
# 2011-08-01 Fix nonblock test (broken at v13).
# 2011-08-01 Fix autoconf 2.68 warnings
# 2011-06-23 Add ACX_CHECK_FLTO to check -flto.
# 2010-08-16 Fix FLAG_OMITTED for AS_TR_CPP changes in autoconf-2.66.
# 2010-07-02 Add check for ss_family (for minix).
# 2010-04-26 Fix to use CPPFLAGS for CHECK_COMPILER_FLAGS.
# 2010-03-01 Fix RPATH using CONFIG_COMMANDS to run at the very end.
# 2010-02-18 WITH_SSL outputs the LIBSSL_LDFLAGS, LIBS, CPPFLAGS separate, -ldl
# 2010-02-01 added ACX_CHECK_MEMCMP_SIGNED, AHX_MEMCMP_BROKEN
# 2010-01-20 added AHX_COONFIG_STRLCAT
# 2009-07-14 U_CHAR detection improved for windows crosscompile.
#            added ACX_FUNC_MALLOC
#            fixup some #if to #ifdef
#            NONBLOCKING test for mingw crosscompile.
# 2009-07-13 added ACX_WITH_SSL_OPTIONAL
# 2009-07-03 fixup LDFLAGS for empty ssl dir.
#
# Automates some of the checking constructs.  Aims at portability for POSIX.
# Documentation for functions is below.
#
# the following macro's are provided in this file:
# (see below for details on each macro).
#
# ACX_ESCAPE_BACKSLASH		- escape backslashes in var for C-preproc.
# ACX_RSRC_VERSION		- create windows resource version number.
# ACX_CHECK_COMPILER_FLAG	- see if cc supports a flag.
# ACX_CHECK_ERROR_FLAGS		- see which flag is -werror (used below).
# ACX_CHECK_COMPILER_FLAG_NEEDED - see if flags make the code compile cleanly.
# ACX_DEPFLAG			- find cc dependency flags.
# ACX_DETERMINE_EXT_FLAGS_UNBOUND - find out which flags enable BSD and POSIX.
# ACX_CHECK_FORMAT_ATTRIBUTE	- find cc printf format syntax.
# ACX_CHECK_UNUSED_ATTRIBUTE	- find cc variable unused syntax.
# ACX_CHECK_FLTO		- see if cc supports -flto and use it if so.
# ACX_LIBTOOL_C_ONLY		- create libtool for C only, improved.
# ACX_TYPE_U_CHAR		- u_char type.
# ACX_TYPE_RLIM_T		- rlim_t type.
# ACX_TYPE_SOCKLEN_T		- socklen_t type.
# ACX_TYPE_IN_ADDR_T		- in_addr_t type.
# ACX_TYPE_IN_PORT_T		- in_port_t type.
# ACX_ARG_RPATH			- add --disable-rpath option.
# ACX_WITH_SSL			- add --with-ssl option, link -lcrypto.
# ACX_WITH_SSL_OPTIONAL		- add --with-ssl option, link -lcrypto,
#				  where --without-ssl is also accepted
# ACX_LIB_SSL			- setup to link -lssl.
# ACX_SYS_LARGEFILE		- improved sys_largefile, fseeko, >2G files.
# ACX_CHECK_GETADDRINFO_WITH_INCLUDES - find getaddrinfo, portably.
# ACX_FUNC_DEPRECATED		- see if func is deprecated.
# ACX_CHECK_NONBLOCKING_BROKEN	- see if nonblocking sockets really work.
# ACX_MKDIR_ONE_ARG		- determine mkdir(2) number of arguments.
# ACX_FUNC_IOCTLSOCKET		- find ioctlsocket, portably.
# ACX_FUNC_MALLOC		- check malloc, define replacement .
# AHX_CONFIG_FORMAT_ATTRIBUTE	- config.h text for format.
# AHX_CONFIG_UNUSED_ATTRIBUTE	- config.h text for unused.
# AHX_CONFIG_FSEEKO		- define fseeko, ftello fallback.
# AHX_CONFIG_RAND_MAX		- define RAND_MAX if needed.
# AHX_CONFIG_MAXHOSTNAMELEN	- define MAXHOSTNAMELEN if needed.
# AHX_CONFIG_IPV6_MIN_MTU	- define IPV6_MIN_MTU if needed.
# AHX_CONFIG_SNPRINTF		- snprintf compat prototype
# AHX_CONFIG_INET_PTON		- inet_pton compat prototype
# AHX_CONFIG_INET_NTOP		- inet_ntop compat prototype
# AHX_CONFIG_INET_ATON		- inet_aton compat prototype
# AHX_CONFIG_MEMMOVE		- memmove compat prototype
# AHX_CONFIG_STRLCAT		- strlcat compat prototype
# AHX_CONFIG_STRLCPY		- strlcpy compat prototype
# AHX_CONFIG_GMTIME_R		- gmtime_r compat prototype
# AHX_CONFIG_W32_SLEEP		- w32 compat for sleep
# AHX_CONFIG_W32_USLEEP		- w32 compat for usleep
# AHX_CONFIG_W32_RANDOM		- w32 compat for random
# AHX_CONFIG_W32_SRANDOM	- w32 compat for srandom
# AHX_CONFIG_W32_FD_SET_T	- w32 detection of FD_SET_T.
# ACX_CFLAGS_STRIP		- strip one flag from CFLAGS
# ACX_STRIP_EXT_FLAGS		- strip extension flags from CFLAGS
# AHX_CONFIG_FLAG_OMITTED	- define omitted flag
# AHX_CONFIG_FLAG_EXT		- define omitted extension flag
# AHX_CONFIG_EXT_FLAGS		- define the stripped extension flags
# ACX_CHECK_MEMCMP_SIGNED	- check if memcmp uses signed characters.
# AHX_MEMCMP_BROKEN		- replace memcmp func for CHECK_MEMCMP_SIGNED.
# ACX_CHECK_SS_FAMILY           - check for sockaddr_storage.ss_family
# ACX_CHECK_PIE			- add --enable-pie option and check if works
# ACX_CHECK_RELRO_NOW		- add --enable-relro-now option and check it
#

dnl Escape backslashes as \\, for C:\ paths, for the C preprocessor defines.
dnl for example, ACX_ESCAPE_BACKSLASH($from_var, to_var)
dnl $1: the text to change. 
dnl $2: the result.
AC_DEFUN([ACX_ESCAPE_BACKSLASH], [$2="`echo $1 | sed -e 's/\\\\/\\\\\\\\/g'`"
])

dnl Calculate comma separated windows-resource numbers from package version.
dnl Picks the first three(,0) or four numbers out of the name.
dnl $1: variable for the result
AC_DEFUN([ACX_RSRC_VERSION], 
[$1=[`echo $PACKAGE_VERSION | sed -e 's/^[^0-9]*\([0-9][0-9]*\)[^0-9][^0-9]*\([0-9][0-9]*\)[^0-9][^0-9]*\([0-9][0-9]*\)[^0-9][^0-9]*\([0-9][0-9]*\).*$/\1,\2,\3,\4/' -e 's/^[^0-9]*\([0-9][0-9]*\)[^0-9][^0-9]*\([0-9][0-9]*\)[^0-9][^0-9]*\([0-9][0-9]*\)[^0-9]*$/\1,\2,\3,0/' `]
])

dnl Routine to help check for compiler flags.
dnl Checks if the compiler will accept the flag.
dnl $1: the flag without a - in front, so g to check -g.
dnl $2: executed if yes
dnl $3: executed if no
AC_DEFUN([ACX_CHECK_COMPILER_FLAG], 
[
AC_REQUIRE([AC_PROG_CC])
AC_MSG_CHECKING(whether $CC supports -$1)
cache=`echo $1 | sed 'y%.=/+-%___p_%'`
AC_CACHE_VAL(cv_prog_cc_flag_$cache,
[
echo 'void f(void){}' >conftest.c
if test -z "`$CC $CPPFLAGS $CFLAGS -$1 -c conftest.c 2>&1`"; then
eval "cv_prog_cc_flag_$cache=yes"
else
eval "cv_prog_cc_flag_$cache=no"
fi
rm -f conftest conftest.o conftest.c
])
if eval "test \"`echo '$cv_prog_cc_flag_'$cache`\" = yes"; then
AC_MSG_RESULT(yes)
:
$2
else
AC_MSG_RESULT(no)
:
$3
fi
])

dnl setup flags for ACX_CHECK_COMPILER_FLAG_NEEDED
dnl ERRFLAG: result, compiler flag to turn warnings into errors
AC_DEFUN([ACX_CHECK_ERROR_FLAGS],
[
ACX_CHECK_COMPILER_FLAG(Werror, [ERRFLAG="-Werror"], [ERRFLAG="-errwarn"])
ACX_CHECK_COMPILER_FLAG(Wall, [ERRFLAG="$ERRFLAG -Wall"],
                        	[ERRFLAG="$ERRFLAG -errfmt"])
])

dnl Routine to help check for needed compiler flags.
dnl $1: flags for CC
dnl $2: the includes and code
dnl $3: if the given code only compiles with the flag, execute argument 3
dnl $4: if the given code compiles without the flag, execute argument 4
dnl $5: with and without flag the compile fails, execute argument 5.
AC_DEFUN([ACX_CHECK_COMPILER_FLAG_NEEDED],
[
AC_REQUIRE([AC_PROG_CC])
AC_REQUIRE([ACX_CHECK_ERROR_FLAGS])
AC_MSG_CHECKING(whether we need $1 as a flag for $CC)
cache=AS_TR_SH($1)
dnl cache=`echo $1 | sed 'y%.=/+- %___p__%'`
AC_CACHE_VAL(cv_prog_cc_flag_needed_$cache,
[
echo '$2' > conftest.c
echo 'void f(){}' >>conftest.c
if test -z "`$CC $CPPFLAGS $CFLAGS $ERRFLAG -c conftest.c 2>&1`"; then
eval "cv_prog_cc_flag_needed_$cache=no"
else
[
if test -z "`$CC $CPPFLAGS $CFLAGS $1 $ERRFLAG -c conftest.c 2>&1`"; then
eval "cv_prog_cc_flag_needed_$cache=yes"
else
eval "cv_prog_cc_flag_needed_$cache=fail"
#echo 'Test with flag fails too!'
#cat conftest.c
#echo "$CC $CPPFLAGS $CFLAGS $1 $ERRFLAG -c conftest.c 2>&1"
#echo `$CC $CPPFLAGS $CFLAGS $1 $ERRFLAG -c conftest.c 2>&1`
#exit 1
fi
]
fi
rm -f conftest conftest.c conftest.o
])
if eval "test \"`echo '$cv_prog_cc_flag_needed_'$cache`\" = yes"; then
AC_MSG_RESULT(yes)
:
$3
else
if eval "test \"`echo '$cv_prog_cc_flag_needed_'$cache`\" = no"; then
AC_MSG_RESULT(no)
#echo 'Test with flag is no!'
#cat conftest.c
#echo "$CC $CPPFLAGS $CFLAGS $1 $ERRFLAG -c conftest.c 2>&1"
#echo `$CC $CPPFLAGS $CFLAGS $1 $ERRFLAG -c conftest.c 2>&1`
#exit 1
:
$4
else
AC_MSG_RESULT(failed)
:
$5
fi
fi
])

dnl Check for CC dependency flag
dnl DEPFLAG: set to flag that generates dependencies.
AC_DEFUN([ACX_DEPFLAG],
[
AC_MSG_CHECKING([$CC dependency flag])
echo 'void f(){}' >conftest.c
if test "`$CC -MM conftest.c 2>&1`" = "conftest.o: conftest.c"; then
	DEPFLAG="-MM"
else 
  if test "`$CC -xM1 conftest.c 2>&1`" = "conftest.o: conftest.c"; then
	DEPFLAG="-xM1"
  else
	DEPFLAG="-MM"  # dunno do something
  fi 
fi
AC_MSG_RESULT($DEPFLAG)
rm -f conftest.c
AC_SUBST(DEPFLAG)
])

dnl Determine flags that gives POSIX and BSD functionality.
dnl CFLAGS is modified for the result.
AC_DEFUN([ACX_DETERMINE_EXT_FLAGS_UNBOUND],
[
ACX_CHECK_COMPILER_FLAG(std=c99, [C99FLAG="-std=c99"])
ACX_CHECK_COMPILER_FLAG(xc99, [C99FLAG="-xc99"])

AC_CHECK_HEADERS([getopt.h time.h],,, [AC_INCLUDES_DEFAULT])

ACX_CHECK_COMPILER_FLAG_NEEDED($C99FLAG -D__EXTENSIONS__ -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112 -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED=1 -D_ALL_SOURCE,
[
#include "confdefs.h"
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#include <unistd.h>
#include <netdb.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

int test() {
	int a;
	char **opts = NULL;
	struct timeval tv;
	char *t;
	time_t time = 0;
	char *buf = NULL;
	const char* str = NULL;
	struct msghdr msg;
	msg.msg_control = 0;
	t = ctime_r(&time, buf);
	tv.tv_usec = 10;
	srandom(32);
	a = getopt(2, opts, "a");
	a = isascii(32);
	str = gai_strerror(0);
	if(str && t && tv.tv_usec && msg.msg_control)
		a = 0;
	return a;
}
], [CFLAGS="$CFLAGS $C99FLAG -D__EXTENSIONS__ -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112 -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED=1 -D_ALL_SOURCE"])

ACX_CHECK_COMPILER_FLAG_NEEDED($C99FLAG -D__EXTENSIONS__ -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112 -D_XOPEN_SOURCE=600 -D_ALL_SOURCE,
[
#include "confdefs.h"
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#include <unistd.h>
#include <netdb.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

int test() {
	int a;
	char **opts = NULL;
	struct timeval tv;
	char *t;
	time_t time = 0;
	char *buf = NULL;
	const char* str = NULL;
	struct msghdr msg;
	msg.msg_control = 0;
	t = ctime_r(&time, buf);
	tv.tv_usec = 10;
	srandom(32);
	a = getopt(2, opts, "a");
	a = isascii(32);
	str = gai_strerror(0);
	if(str && t && tv.tv_usec && msg.msg_control)
		a = 0;
	return a;
}
], [CFLAGS="$CFLAGS $C99FLAG -D__EXTENSIONS__ -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112 -D_XOPEN_SOURCE=600 -D_ALL_SOURCE"])

ACX_CHECK_COMPILER_FLAG_NEEDED($C99FLAG,
[
#include <stdbool.h>
#include <ctype.h>
int test() {
        int a = 0;
        return a;
}
], [CFLAGS="$CFLAGS $C99FLAG"])

ACX_CHECK_COMPILER_FLAG_NEEDED(-D_BSD_SOURCE -D_DEFAULT_SOURCE,
[
#include <ctype.h>

int test() {
        int a;
        a = isascii(32);
        return a;
}
], [CFLAGS="$CFLAGS -D_BSD_SOURCE -D_DEFAULT_SOURCE"])

ACX_CHECK_COMPILER_FLAG_NEEDED(-D_GNU_SOURCE,
[
#include <netinet/in.h>

int test() {
        struct in6_pktinfo inf;
	int a = (int)sizeof(inf);
        return a;
}
], [CFLAGS="$CFLAGS -D_GNU_SOURCE"])

# check again for GNU_SOURCE for setresgid. May fail if setresgid
# is not available at all. -D_FRSRESGID is to make this check unique.
# otherwise we would get the previous cached result.
ACX_CHECK_COMPILER_FLAG_NEEDED(-D_GNU_SOURCE -D_FRSRESGID,
[
#include <unistd.h>

int test() {
	int a = setresgid(0,0,0);
	a = setresuid(0,0,0);
        return a;
}
], [CFLAGS="$CFLAGS -D_GNU_SOURCE"])

ACX_CHECK_COMPILER_FLAG_NEEDED(-D_POSIX_C_SOURCE=200112,
[
#include "confdefs.h"
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#include <netdb.h>

int test() {
        int a = 0;
        char *t;
        time_t time = 0;
        char *buf = NULL;
	const char* str = NULL;
        t = ctime_r(&time, buf);
	str = gai_strerror(0);
	if(t && str)
		a = 0;
        return a;
}
], [CFLAGS="$CFLAGS -D_POSIX_C_SOURCE=200112"])

ACX_CHECK_COMPILER_FLAG_NEEDED(-D__EXTENSIONS__,
[
#include "confdefs.h"
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#include <unistd.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

int test() {
        int a;
        char **opts = NULL;
        struct timeval tv;
        tv.tv_usec = 10;
        srandom(32);
        a = getopt(2, opts, "a");
        a = isascii(32);
	if(tv.tv_usec)
		a = 0;
        return a;
}
], [CFLAGS="$CFLAGS -D__EXTENSIONS__"])

])dnl End of ACX_DETERMINE_EXT_FLAGS_UNBOUND

dnl Check if CC supports -flto.
dnl in a way that supports clang and suncc (that flag does something else,
dnl but fails to link).  It sets it in CFLAGS if it works.
AC_DEFUN([ACX_CHECK_FLTO], [
    AC_ARG_ENABLE([flto], AS_HELP_STRING([--disable-flto], [Disable link-time optimization (gcc specific option)]))
    AS_IF([test "x$enable_flto" != "xno"], [
        AC_MSG_CHECKING([if $CC supports -flto])
        BAKCFLAGS="$CFLAGS"
        CFLAGS="$CFLAGS -flto"
        AC_LINK_IFELSE([AC_LANG_PROGRAM([], [])], [
            if $CC $CFLAGS -o conftest conftest.c 2>&1 | $GREP -e "warning: no debug symbols in executable" -e "warning: object" >/dev/null; then
                CFLAGS="$BAKCFLAGS"
                AC_MSG_RESULT(no)
            else
                AC_MSG_RESULT(yes)
            fi
            rm -f conftest conftest.c conftest.o
        ], [CFLAGS="$BAKCFLAGS" ; AC_MSG_RESULT(no)])
    ])
])

dnl Check the printf-format attribute (if any)
dnl result in HAVE_ATTR_FORMAT.  
dnl Make sure you also include the AHX_CONFIG_FORMAT_ATTRIBUTE.
AC_DEFUN([ACX_CHECK_FORMAT_ATTRIBUTE],
[AC_REQUIRE([AC_PROG_CC])
AC_MSG_CHECKING(whether the C compiler (${CC-cc}) accepts the "format" attribute)
AC_CACHE_VAL(ac_cv_c_format_attribute,
[ac_cv_c_format_attribute=no
AC_TRY_COMPILE(
[#include <stdio.h>
void f (char *format, ...) __attribute__ ((format (printf, 1, 2)));
void (*pf) (char *format, ...) __attribute__ ((format (printf, 1, 2)));
], [
   f ("%s", "str");
],
[ac_cv_c_format_attribute="yes"],
[ac_cv_c_format_attribute="no"])
])

AC_MSG_RESULT($ac_cv_c_format_attribute)
if test $ac_cv_c_format_attribute = yes; then
  AC_DEFINE(HAVE_ATTR_FORMAT, 1, [Whether the C compiler accepts the "format" attribute])
fi
])dnl End of ACX_CHECK_FORMAT_ATTRIBUTE

dnl Setup ATTR_FORMAT config.h parts.
dnl make sure you call ACX_CHECK_FORMAT_ATTRIBUTE also.
AC_DEFUN([AHX_CONFIG_FORMAT_ATTRIBUTE],
[ 
#ifdef HAVE_ATTR_FORMAT
#  define ATTR_FORMAT(archetype, string_index, first_to_check) \
    __attribute__ ((format (archetype, string_index, first_to_check)))
#else /* !HAVE_ATTR_FORMAT */
#  define ATTR_FORMAT(archetype, string_index, first_to_check) /* empty */
#endif /* !HAVE_ATTR_FORMAT */
])

dnl Check how to mark function arguments as unused.
dnl result in HAVE_ATTR_UNUSED.  
dnl Make sure you include AHX_CONFIG_UNUSED_ATTRIBUTE also.
AC_DEFUN([ACX_CHECK_UNUSED_ATTRIBUTE],
[AC_REQUIRE([AC_PROG_CC])
AC_MSG_CHECKING(whether the C compiler (${CC-cc}) accepts the "unused" attribute)
AC_CACHE_VAL(ac_cv_c_unused_attribute,
[ac_cv_c_unused_attribute=no
AC_TRY_COMPILE(
[#include <stdio.h>
void f (char *u __attribute__((unused)));
], [
   f ("x");
],
[ac_cv_c_unused_attribute="yes"],
[ac_cv_c_unused_attribute="no"])
])

dnl Setup ATTR_UNUSED config.h parts.
dnl make sure you call ACX_CHECK_UNUSED_ATTRIBUTE also.
AC_DEFUN([AHX_CONFIG_UNUSED_ATTRIBUTE],
[
#if defined(DOXYGEN)
#  define ATTR_UNUSED(x)  x
#elif defined(__cplusplus)
#  define ATTR_UNUSED(x)
#elif defined(HAVE_ATTR_UNUSED)
#  define ATTR_UNUSED(x)  x __attribute__((unused))
#else /* !HAVE_ATTR_UNUSED */
#  define ATTR_UNUSED(x)  x
#endif /* !HAVE_ATTR_UNUSED */
])

AC_MSG_RESULT($ac_cv_c_unused_attribute)
if test $ac_cv_c_unused_attribute = yes; then
  AC_DEFINE(HAVE_ATTR_UNUSED, 1, [Whether the C compiler accepts the "unused" attribute])
fi
])dnl

dnl Pre-fun for ACX_LIBTOOL_C_ONLY
AC_DEFUN([ACX_LIBTOOL_C_PRE], [
# skip these tests, we do not need them.
AC_DEFUN([AC_PROG_F77], [:])
AC_DEFUN([AC_PROG_FC], [:])
AC_DEFUN([AC_PROG_CXX], [:])
AC_DEFUN([AC_PROG_CXXCPP], [:])
AC_DEFUN([AC_PROG_OBJC], [:])
AC_DEFUN([AC_PROG_OBJCCPP], [:])
AC_DEFUN([AC_LIBTOOL_CXX], [:])
AC_DEFUN([AC_LIBTOOL_F77], [:])
# always use ./libtool unless override from commandline (libtool=mylibtool)
if test -z "$libtool"; then
	libtool="./libtool"
fi
AC_SUBST(libtool)
# avoid libtool max commandline length test on systems that fork slowly.
AC_CANONICAL_HOST
if echo "$host_os" | grep "sunos4" >/dev/null; then
	lt_cv_sys_max_cmd_len=32750;
fi
AC_PATH_TOOL(AR, ar, [false])
if test $AR = false; then
	AC_MSG_ERROR([Cannot find 'ar', please extend PATH to include it])
fi
])

dnl Perform libtool check, portably, only for C
AC_DEFUN([ACX_LIBTOOL_C_ONLY], [
dnl as a requirement so that is gets called before LIBTOOL
dnl because libtools 'AC_REQUIRE' names are right after this one, before
dnl this function contents.
AC_REQUIRE([ACX_LIBTOOL_C_PRE])
AC_PROG_LIBTOOL
])

dnl Detect if u_char type is defined, otherwise define it.
AC_DEFUN([ACX_TYPE_U_CHAR], 
[AC_CHECK_TYPE([u_char], ,
	[AC_DEFINE([u_char], [unsigned char], [Define to 'unsigned char if not defined])], [
AC_INCLUDES_DEFAULT
#ifdef HAVE_WINSOCK2_H
#  include <winsock2.h>
#endif
]) ])

dnl Detect if rlim_t type is defined, otherwise define it.
AC_DEFUN([ACX_TYPE_RLIM_T],
[AC_CHECK_TYPE(rlim_t, , 
	[AC_DEFINE([rlim_t], [unsigned long], [Define to 'int' if not defined])], [
AC_INCLUDES_DEFAULT
#ifdef HAVE_SYS_RESOURCE_H
#  include <sys/resource.h>
#endif
]) ])

dnl Detect if socklen_t type is defined, otherwise define it.
AC_DEFUN([ACX_TYPE_SOCKLEN_T],
[
AC_CHECK_TYPE(socklen_t, , 
	[AC_DEFINE([socklen_t], [int], [Define to 'int' if not defined])], [
AC_INCLUDES_DEFAULT
#ifdef HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
#endif
#ifdef HAVE_WS2TCPIP_H
#  include <ws2tcpip.h>
#endif
]) ])

dnl Detect if in_addr_t type is defined, otherwise define it.
AC_DEFUN([ACX_TYPE_IN_ADDR_T],
[ AC_CHECK_TYPE(in_addr_t, [], [AC_DEFINE([in_addr_t], [uint32_t], [in_addr_t])], [
AC_INCLUDES_DEFAULT
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
]) ])

dnl Detect if in_port_t type is defined, otherwise define it.
AC_DEFUN([ACX_TYPE_IN_PORT_T],
[ AC_CHECK_TYPE(in_port_t, [], [AC_DEFINE([in_port_t], [uint16_t], [in_port_t])], [
AC_INCLUDES_DEFAULT
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
]) ])

dnl Add option to disable the evil rpath. Check whether to use rpath or not.
dnl Adds the --disable-rpath option. Uses trick to edit the ./libtool.
AC_DEFUN([ACX_ARG_RPATH],
[
AC_ARG_ENABLE(rpath,
        [  --disable-rpath         disable hardcoded rpath (default=enabled)],
	enable_rpath=$enableval, enable_rpath=yes)
if test "x$enable_rpath" = xno; then
	dnl AC_MSG_RESULT([Fixing libtool for -rpath problems.])
	AC_CONFIG_COMMANDS([disable-rpath], [
	sed < libtool > libtool-2 \
	's/^hardcode_libdir_flag_spec.*$'/'hardcode_libdir_flag_spec=" -D__LIBTOOL_RPATH_SED__ "/'
	mv libtool-2 libtool
	chmod 755 libtool
	libtool="./libtool"
	])
fi
])

dnl Add a -R to the RUNTIME_PATH.  Only if rpath is enabled and it is
dnl an absolute path.
dnl $1: the pathname to add.
AC_DEFUN([ACX_RUNTIME_PATH_ADD], [
	if test "x$enable_rpath" = xyes; then
		if echo "$1" | grep "^/" >/dev/null; then
			RUNTIME_PATH="$RUNTIME_PATH -R$1"
		fi
	fi
])

dnl Common code for both ACX_WITH_SSL and ACX_WITH_SSL_OPTIONAL
dnl Takes one argument; the withval checked in those 2 functions
dnl sets up the environment for the given openssl path
AC_DEFUN([ACX_SSL_CHECKS], [
    withval=$1
    if test x_$withval != x_no; then
        AC_MSG_CHECKING(for SSL)
        if test x_$withval = x_ -o x_$withval = x_yes; then
            withval="/usr/local/ssl /usr/lib/ssl /usr/ssl /usr/pkg /usr/local /opt/local /usr/sfw /usr"
        fi
        for dir in $withval; do
            ssldir="$dir"
            if test -f "$dir/include/openssl/ssl.h"; then
                found_ssl="yes"
                AC_DEFINE_UNQUOTED([HAVE_SSL], [], [Define if you have the SSL libraries installed.])
                dnl assume /usr/include is already in the include-path.
                if test "$ssldir" != "/usr"; then
                        CPPFLAGS="$CPPFLAGS -I$ssldir/include"
                        LIBSSL_CPPFLAGS="$LIBSSL_CPPFLAGS -I$ssldir/include"
                fi
                break;
            fi
        done
        if test x_$found_ssl != x_yes; then
            AC_MSG_ERROR(Cannot find the SSL libraries in $withval)
        else
            AC_MSG_RESULT(found in $ssldir)
            HAVE_SSL=yes
            dnl assume /usr is already in the lib and dynlib paths.
            if test "$ssldir" != "/usr" -a "$ssldir" != ""; then
                LDFLAGS="$LDFLAGS -L$ssldir/lib"
                LIBSSL_LDFLAGS="$LIBSSL_LDFLAGS -L$ssldir/lib"
                ACX_RUNTIME_PATH_ADD([$ssldir/lib])
            fi
        
            AC_MSG_CHECKING([for HMAC_Update in -lcrypto])
            LIBS="$LIBS -lcrypto"
            LIBSSL_LIBS="$LIBSSL_LIBS -lcrypto"
            AC_TRY_LINK(, [
                int HMAC_Update(void);
                (void)HMAC_Update();
              ], [
                AC_MSG_RESULT(yes)
                AC_DEFINE([HAVE_HMAC_UPDATE], 1, 
                          [If you have HMAC_Update])
              ], [
                AC_MSG_RESULT(no)
                # check if -lwsock32 or -lgdi32 are needed.	
                BAKLIBS="$LIBS"
                BAKSSLLIBS="$LIBSSL_LIBS"
                LIBS="$LIBS -lgdi32"
                LIBSSL_LIBS="$LIBSSL_LIBS -lgdi32"
                AC_MSG_CHECKING([if -lcrypto needs -lgdi32])
                AC_TRY_LINK([], [
                    int HMAC_Update(void);
                    (void)HMAC_Update();
                  ],[
                    AC_DEFINE([HAVE_HMAC_UPDATE], 1, 
                        [If you have HMAC_Update])
                    AC_MSG_RESULT(yes) 
                  ],[
                    AC_MSG_RESULT(no)
                    LIBS="$BAKLIBS"
                    LIBSSL_LIBS="$BAKSSLLIBS"
                    LIBS="$LIBS -ldl"
                    LIBSSL_LIBS="$LIBSSL_LIBS -ldl"
                    AC_MSG_CHECKING([if -lcrypto needs -ldl])
                    AC_TRY_LINK([], [
                        int HMAC_Update(void);
                        (void)HMAC_Update();
                      ],[
                        AC_DEFINE([HAVE_HMAC_UPDATE], 1, 
                            [If you have HMAC_Update])
                        AC_MSG_RESULT(yes) 
                      ],[
                        AC_MSG_RESULT(no)
                        LIBS="$BAKLIBS"
                        LIBSSL_LIBS="$BAKSSLLIBS"
                        LIBS="$LIBS -ldl -pthread"
                        LIBSSL_LIBS="$LIBSSL_LIBS -ldl -pthread"
                        AC_MSG_CHECKING([if -lcrypto needs -ldl -pthread])
                        AC_TRY_LINK([], [
                            int HMAC_Update(void);
                            (void)HMAC_Update();
                          ],[
                            AC_DEFINE([HAVE_HMAC_UPDATE], 1, 
                                [If you have HMAC_Update])
                            AC_MSG_RESULT(yes) 
                          ],[
                            AC_MSG_RESULT(no)
                            AC_MSG_ERROR([OpenSSL found in $ssldir, but version 0.9.7 or higher is required])
			])
                    ])
                ])
            ])
        fi
        AC_SUBST(HAVE_SSL)
        AC_SUBST(RUNTIME_PATH)
    fi
AC_CHECK_HEADERS([openssl/ssl.h],,, [AC_INCLUDES_DEFAULT])
AC_CHECK_HEADERS([openssl/err.h],,, [AC_INCLUDES_DEFAULT])
AC_CHECK_HEADERS([openssl/rand.h],,, [AC_INCLUDES_DEFAULT])
])dnl End of ACX_SSL_CHECKS

dnl Check for SSL, where SSL is mandatory
dnl Adds --with-ssl option, searches for openssl and defines HAVE_SSL if found
dnl Setup of CPPFLAGS, CFLAGS.  Adds -lcrypto to LIBS. 
dnl Checks main header files of SSL.
dnl
AC_DEFUN([ACX_WITH_SSL],
[
AC_ARG_WITH(ssl, AC_HELP_STRING([--with-ssl=pathname],
                                    [enable SSL (will check /usr/local/ssl
                            /usr/lib/ssl /usr/ssl /usr/pkg /usr/local /opt/local /usr/sfw /usr)]),[
        ],[
            withval="yes"
        ])
    if test x_$withval = x_no; then
	AC_MSG_ERROR([Need SSL library to do digital signature cryptography])
    fi
    ACX_SSL_CHECKS($withval)
])dnl End of ACX_WITH_SSL

dnl Check for SSL, where ssl is optional (--without-ssl is allowed)
dnl Adds --with-ssl option, searches for openssl and defines HAVE_SSL if found
dnl Setup of CPPFLAGS, CFLAGS.  Adds -lcrypto to LIBS. 
dnl Checks main header files of SSL.
dnl
AC_DEFUN([ACX_WITH_SSL_OPTIONAL],
[
AC_ARG_WITH(ssl, AC_HELP_STRING([--with-ssl=pathname],
                                [enable SSL (will check /usr/local/ssl
                                /usr/lib/ssl /usr/ssl /usr/pkg /usr/local /opt/local /usr/sfw /usr)]),[
        ],[
            withval="yes"
        ])
    ACX_SSL_CHECKS($withval)
])dnl End of ACX_WITH_SSL_OPTIONAL

dnl Setup to use -lssl
dnl To use -lcrypto, use the ACX_WITH_SSL setup (before this one).
AC_DEFUN([ACX_LIB_SSL],
[
# check if libssl needs libdl
BAKLIBS="$LIBS"
LIBS="-lssl $LIBS"
AC_MSG_CHECKING([if libssl needs libdl])
AC_TRY_LINK_FUNC([SSL_CTX_new], [
	AC_MSG_RESULT([no])
	LIBS="$BAKLIBS"
] , [
	AC_MSG_RESULT([yes])
	LIBS="$BAKLIBS"
	AC_SEARCH_LIBS([dlopen], [dl])
]) ])dnl End of ACX_LIB_SSL

dnl Setup to use very large files (>2Gb).
dnl setups fseeko and its own
AC_DEFUN([ACX_SYS_LARGEFILE],
[
AC_SYS_LARGEFILE
dnl try to see if an additional _LARGEFILE_SOURCE 1 is needed to get fseeko
ACX_CHECK_COMPILER_FLAG_NEEDED(-D_LARGEFILE_SOURCE=1,
[
#include <stdio.h>
int test() {
        int a = fseeko(stdin, 0, 0);
        return a;
}
], [CFLAGS="$CFLAGS -D_LARGEFILE_SOURCE=1"])
])

dnl Check getaddrinfo.
dnl Works on linux, solaris, bsd and windows(links winsock).
dnl defines HAVE_GETADDRINFO, USE_WINSOCK.
AC_DEFUN([ACX_CHECK_GETADDRINFO_WITH_INCLUDES],
[AC_REQUIRE([AC_PROG_CC])
AC_MSG_CHECKING(for getaddrinfo)
ac_cv_func_getaddrinfo=no
AC_LINK_IFELSE(
[AC_LANG_SOURCE([[
#ifdef __cplusplus
extern "C"
{
#endif
char* getaddrinfo();
char* (*f) () = getaddrinfo;
#ifdef __cplusplus
}
#endif
int main() {
        ;
        return 0;
}
]])],
dnl this case on linux, solaris, bsd
[ac_cv_func_getaddrinfo="yes"
dnl see if on windows
if test "$ac_cv_header_windows_h" = "yes"; then
	AC_DEFINE(USE_WINSOCK, 1, [Whether the windows socket API is used])
	USE_WINSOCK="1"
	LIBS="$LIBS -lws2_32"
fi
],
dnl no quick getaddrinfo, try mingw32 and winsock2 library.
ORIGLIBS="$LIBS"
LIBS="$LIBS -lws2_32"
AC_LINK_IFELSE(
[AC_LANG_PROGRAM(
[
#ifdef HAVE_WS2TCPIP_H
#include <ws2tcpip.h>
#endif
],
[
        (void)getaddrinfo(NULL, NULL, NULL, NULL);
]
)],
[
ac_cv_func_getaddrinfo="yes"
dnl already: LIBS="$LIBS -lws2_32"
AC_DEFINE(USE_WINSOCK, 1, [Whether the windows socket API is used])
USE_WINSOCK="1"
],
[
ac_cv_func_getaddrinfo="no"
LIBS="$ORIGLIBS"
])
)

AC_MSG_RESULT($ac_cv_func_getaddrinfo)
if test $ac_cv_func_getaddrinfo = yes; then
  AC_DEFINE(HAVE_GETADDRINFO, 1, [Whether getaddrinfo is available])
fi
])dnl Endof AC_CHECK_GETADDRINFO_WITH_INCLUDES

dnl check if a function is deprecated. defines DEPRECATED_func in config.h.
dnl $1: function name
dnl $2: C-statement that calls the function.
dnl $3: includes for the program.
dnl $4: executes if yes
dnl $5: executes if no
AC_DEFUN([ACX_FUNC_DEPRECATED],
[
AC_REQUIRE([AC_PROG_CC])
AC_MSG_CHECKING(if $1 is deprecated)
cache=`echo $1 | sed 'y%.=/+-%___p_%'`
AC_CACHE_VAL(cv_cc_deprecated_$cache,
[
echo '$3' >conftest.c
echo 'void f(){ $2 }' >>conftest.c
if test -z "`$CC -c conftest.c 2>&1 | grep deprecated`"; then
eval "cv_cc_deprecated_$cache=no"
else
eval "cv_cc_deprecated_$cache=yes"
fi
rm -f conftest conftest.o conftest.c
])
if eval "test \"`echo '$cv_cc_deprecated_'$cache`\" = yes"; then
AC_MSG_RESULT(yes)
AC_DEFINE_UNQUOTED(AS_TR_CPP([DEPRECATED_$1]), 1, [Whether $1 is deprecated])
:
$4
else
AC_MSG_RESULT(no)
:
$5
fi
])dnl end of ACX_FUNC_DEPRECATED

dnl check if select and nonblocking sockets actually work.
dnl Needs fork(2) and select(2).
dnl defines NONBLOCKING_IS_BROKEN, and if that is true multiple reads from
dnl a nonblocking socket do not work, a new call to select is necessary.
AC_DEFUN([ACX_CHECK_NONBLOCKING_BROKEN],
[
AC_MSG_CHECKING([if nonblocking sockets work])
if echo $target | grep mingw32 >/dev/null; then 
	AC_MSG_RESULT([no (windows)])
	AC_DEFINE([NONBLOCKING_IS_BROKEN], 1, [Define if the network stack does not fully support nonblocking io (causes lower performance).])
else
AC_RUN_IFELSE([
AC_LANG_SOURCE([[
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif

int main(void)
{
	int port;
	int sfd, cfd;
	int num = 10;
	int i, p;
	struct sockaddr_in a;
	/* test if select and nonblocking reads work well together */
	/* open port.
	   fork child to send 10 messages.
	   select to read.
	   then try to nonblocking read the 10 messages
	   then, nonblocking read must give EAGAIN
	*/

	port = 12345 + (time(0)%32);
	sfd = socket(PF_INET, SOCK_DGRAM, 0);
	if(sfd == -1) {
		perror("socket");
		return 1;
	}
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(port);
	a.sin_addr.s_addr = inet_addr("127.0.0.1");
	if(bind(sfd, (struct sockaddr*)&a, sizeof(a)) < 0) {
		perror("bind");
		return 1;
	}
	if(fcntl(sfd, F_SETFL, O_NONBLOCK) == -1) {
		perror("fcntl");
		return 1;
	}

	cfd = socket(PF_INET, SOCK_DGRAM, 0);
	if(cfd == -1) {
		perror("client socket");
		return 1;
	}
	a.sin_port = 0;
	if(bind(cfd, (struct sockaddr*)&a, sizeof(a)) < 0) {
		perror("client bind");
		return 1;
	}
	a.sin_port = htons(port);

	/* no handler, causes exit in 10 seconds */
	alarm(10);

	/* send and receive on the socket */
	if((p=fork()) == 0) {
		for(i=0; i<num; i++) {
			if(sendto(cfd, &i, sizeof(i), 0, 
				(struct sockaddr*)&a, sizeof(a)) < 0) {
				perror("sendto");
				return 1;
			}
		}
	} else {
		/* parent */
		fd_set rset;
		int x;
		if(p == -1) {
			perror("fork");
			return 1;
		}
		FD_ZERO(&rset);
		FD_SET(sfd, &rset);
		if(select(sfd+1, &rset, NULL, NULL, NULL) < 1) {
			perror("select");
			return 1;
		}
		i = 0;
		while(i < num) {
			if(recv(sfd, &x, sizeof(x), 0) != sizeof(x)) {
				if(errno == EAGAIN)
					continue;
				perror("recv");
				return 1;
			}
			i++;
		}
		/* now we want to get EAGAIN: nonblocking goodness */
		errno = 0;
		recv(sfd, &x, sizeof(x), 0);
		if(errno != EAGAIN) {
			perror("trying to recv again");
			return 1;
		}
		/* EAGAIN encountered */
	}

	close(sfd);
	close(cfd);
	return 0;
}
]])], [
	AC_MSG_RESULT([yes])
], [
	AC_MSG_RESULT([no])
	AC_DEFINE([NONBLOCKING_IS_BROKEN], 1, [Define if the network stack does not fully support nonblocking io (causes lower performance).])
], [
	AC_MSG_RESULT([crosscompile(yes)])
])
fi
])dnl End of ACX_CHECK_NONBLOCKING_BROKEN

dnl Check if mkdir has one or two arguments.
dnl defines MKDIR_HAS_ONE_ARG
AC_DEFUN([ACX_MKDIR_ONE_ARG],
[
AC_MSG_CHECKING([whether mkdir has one arg])
AC_TRY_COMPILE([
#include <stdio.h>
#include <unistd.h>
#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
], [
	(void)mkdir("directory");
],
AC_MSG_RESULT(yes)
AC_DEFINE(MKDIR_HAS_ONE_ARG, 1, [Define if mkdir has one argument.])
,
AC_MSG_RESULT(no)
)
])dnl end of ACX_MKDIR_ONE_ARG

dnl Check for ioctlsocket function. works on mingw32 too.
AC_DEFUN([ACX_FUNC_IOCTLSOCKET],
[
# check ioctlsocket
AC_MSG_CHECKING(for ioctlsocket)
AC_LINK_IFELSE([AC_LANG_PROGRAM([
#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif
], [
	(void)ioctlsocket(0, 0, NULL);
])], [
AC_MSG_RESULT(yes)
AC_DEFINE(HAVE_IOCTLSOCKET, 1, [if the function 'ioctlsocket' is available])
],[AC_MSG_RESULT(no)])
])dnl end of ACX_FUNC_IOCTLSOCKET

dnl detect malloc and provide malloc compat prototype.
dnl $1: unique name for compat code
AC_DEFUN([ACX_FUNC_MALLOC],
[
	AC_MSG_CHECKING([for GNU libc compatible malloc])
	AC_RUN_IFELSE([AC_LANG_PROGRAM(
[[#if defined STDC_HEADERS || defined HAVE_STDLIB_H
#include <stdlib.h>
#else
char *malloc ();
#endif
]], [ if(malloc(0) != 0) return 1;])
],
	[AC_MSG_RESULT([no])
	AC_LIBOBJ(malloc)
	AC_DEFINE_UNQUOTED([malloc], [rpl_malloc_$1], [Define if  replacement function should be used.])] ,
	[AC_MSG_RESULT([yes])
	AC_DEFINE([HAVE_MALLOC], 1, [If have GNU libc compatible malloc])],
	[AC_MSG_RESULT([no (crosscompile)])
	AC_LIBOBJ(malloc)
	AC_DEFINE_UNQUOTED([malloc], [rpl_malloc_$1], [Define if  replacement function should be used.])] )
])

dnl Define fallback for fseeko and ftello if needed.
AC_DEFUN([AHX_CONFIG_FSEEKO],
[
#ifndef HAVE_FSEEKO
#define fseeko fseek
#define ftello ftell
#endif /* HAVE_FSEEKO */
])

dnl Define RAND_MAX if not defined
AC_DEFUN([AHX_CONFIG_RAND_MAX],
[
#ifndef RAND_MAX
#define RAND_MAX	2147483647
#endif
])

dnl Define MAXHOSTNAMELEN if not defined
AC_DEFUN([AHX_CONFIG_MAXHOSTNAMELEN],
[
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif
])

dnl Define IPV6_MIN_MTU if not defined
AC_DEFUN([AHX_CONFIG_IPV6_MIN_MTU],
[
#ifndef IPV6_MIN_MTU
#define IPV6_MIN_MTU 1280
#endif /* IPV6_MIN_MTU */
])

dnl provide snprintf, vsnprintf compat prototype
dnl $1: unique name for compat code
AC_DEFUN([AHX_CONFIG_SNPRINTF],
[
#ifndef HAVE_SNPRINTF
#define snprintf snprintf_$1
#define vsnprintf vsnprintf_$1
#include <stdarg.h>
int snprintf (char *str, size_t count, const char *fmt, ...);
int vsnprintf (char *str, size_t count, const char *fmt, va_list arg);
#endif /* HAVE_SNPRINTF */
])

dnl provide inet_pton compat prototype.
dnl $1: unique name for compat code
AC_DEFUN([AHX_CONFIG_INET_PTON],
[
#ifndef HAVE_INET_PTON
#define inet_pton inet_pton_$1
int inet_pton(int af, const char* src, void* dst);
#endif /* HAVE_INET_PTON */
])

dnl provide inet_ntop compat prototype.
dnl $1: unique name for compat code
AC_DEFUN([AHX_CONFIG_INET_NTOP],
[
#ifndef HAVE_INET_NTOP
#define inet_ntop inet_ntop_$1
const char *inet_ntop(int af, const void *src, char *dst, size_t size);
#endif
])

dnl provide inet_aton compat prototype.
dnl $1: unique name for compat code
AC_DEFUN([AHX_CONFIG_INET_ATON],
[
#ifndef HAVE_INET_ATON
#define inet_aton inet_aton_$1
int inet_aton(const char *cp, struct in_addr *addr);
#endif
])

dnl provide memmove compat prototype.
dnl $1: unique name for compat code
AC_DEFUN([AHX_CONFIG_MEMMOVE],
[
#ifndef HAVE_MEMMOVE
#define memmove memmove_$1
void *memmove(void *dest, const void *src, size_t n);
#endif
])

dnl provide strlcat compat prototype.
dnl $1: unique name for compat code
AC_DEFUN([AHX_CONFIG_STRLCAT],
[
#ifndef HAVE_STRLCAT
#define strlcat strlcat_$1
size_t strlcat(char *dst, const char *src, size_t siz);
#endif
])

dnl provide strlcpy compat prototype.
dnl $1: unique name for compat code
AC_DEFUN([AHX_CONFIG_STRLCPY],
[
#ifndef HAVE_STRLCPY
#define strlcpy strlcpy_$1
size_t strlcpy(char *dst, const char *src, size_t siz);
#endif
])

dnl provide gmtime_r compat prototype.
dnl $1: unique name for compat code
AC_DEFUN([AHX_CONFIG_GMTIME_R],
[
#ifndef HAVE_GMTIME_R
#define gmtime_r gmtime_r_$1
struct tm *gmtime_r(const time_t *timep, struct tm *result);
#endif
])

dnl provide reallocarray compat prototype.
dnl $1: unique name for compat code
AC_DEFUN([AHX_CONFIG_REALLOCARRAY],
[
#ifndef HAVE_REALLOCARRAY
#define reallocarray reallocarray$1
void* reallocarray(void *ptr, size_t nmemb, size_t size);
#endif
])

dnl provide w32 compat definition for sleep
AC_DEFUN([AHX_CONFIG_W32_SLEEP],
[
#if !defined(HAVE_SLEEP) || defined(HAVE_WINDOWS_H)
#define sleep(x) Sleep((x)*1000) /* on win32 */
#endif /* HAVE_SLEEP */
])

dnl provide w32 compat definition for usleep
AC_DEFUN([AHX_CONFIG_W32_USLEEP],
[
#ifndef HAVE_USLEEP
#define usleep(x) Sleep((x)/1000 + 1) /* on win32 */
#endif /* HAVE_USLEEP */
])

dnl provide w32 compat definition for random
AC_DEFUN([AHX_CONFIG_W32_RANDOM],
[
#ifndef HAVE_RANDOM
#define random rand /* on win32, for tests only (bad random) */
#endif /* HAVE_RANDOM */
])

dnl provide w32 compat definition for srandom
AC_DEFUN([AHX_CONFIG_W32_SRANDOM],
[
#ifndef HAVE_SRANDOM
#define srandom(x) srand(x) /* on win32, for tests only (bad random) */
#endif /* HAVE_SRANDOM */
])

dnl provide w32 compat definition for FD_SET_T
AC_DEFUN([AHX_CONFIG_W32_FD_SET_T],
[
/* detect if we need to cast to unsigned int for FD_SET to avoid warnings */
#ifdef HAVE_WINSOCK2_H
#define FD_SET_T (u_int)
#else
#define FD_SET_T 
#endif
])

dnl Remove an extension flag from CFLAGS, define replacement to be made.
dnl Used by ACX_STRIP_EXT_FLAGS.
dnl $1: the name of the flag, for example -D_GNU_SOURCE.
AC_DEFUN([ACX_CFLAGS_STRIP],
[
  if echo $CFLAGS | grep " $1" >/dev/null 2>&1; then
    CFLAGS="`echo $CFLAGS | sed -e 's/ $1//g'`"
    AC_DEFINE(m4_bpatsubst(OMITTED_$1,[[-=]],_), 1, Put $1 define in config.h)
  fi
])

dnl Remove EXT flags from the CFLAGS and set them to be defined in config.h
dnl use with ACX_DETERMINE_EXT_FLAGS.
AC_DEFUN([ACX_STRIP_EXT_FLAGS],
[
  AC_MSG_NOTICE([Stripping extension flags...])
  ACX_CFLAGS_STRIP(-D_GNU_SOURCE)
  ACX_CFLAGS_STRIP(-D_BSD_SOURCE)
  ACX_CFLAGS_STRIP(-D_DEFAULT_SOURCE)
  ACX_CFLAGS_STRIP(-D__EXTENSIONS__)
  ACX_CFLAGS_STRIP(-D_POSIX_C_SOURCE=200112)
  ACX_CFLAGS_STRIP(-D_XOPEN_SOURCE=600)
  ACX_CFLAGS_STRIP(-D_XOPEN_SOURCE_EXTENDED=1)
  ACX_CFLAGS_STRIP(-D_ALL_SOURCE)
  ACX_CFLAGS_STRIP(-D_LARGEFILE_SOURCE=1)
]) dnl End of ACX_STRIP_EXT_FLAGS

dnl define one omitted flag for config.h
dnl $1: flag name. -D_GNU_SOURCE
dnl $2: replacement define. _GNU_SOURCE
dnl $3: define value, 1
AC_DEFUN([AHX_CONFIG_FLAG_OMITTED],
[#if defined($1) && !defined($2)
#define $2 $3
[#]endif ])

dnl Wrapper for AHX_CONFIG_FLAG_OMITTED for -D style flags
dnl $1: the -DNAME or -DNAME=value string.
AC_DEFUN([AHX_CONFIG_FLAG_EXT],
[AHX_CONFIG_FLAG_OMITTED(m4_bpatsubst(OMITTED_$1,[[-=]],_),m4_bpatsubst(m4_bpatsubst($1,-D,),=.*$,),m4_if(m4_bregexp($1,=),-1,1,m4_bpatsubst($1,^.*=,)))
])

dnl config.h part to define omitted cflags, use with ACX_STRIP_EXT_FLAGS.
AC_DEFUN([AHX_CONFIG_EXT_FLAGS],
[AHX_CONFIG_FLAG_EXT(-D_GNU_SOURCE)
AHX_CONFIG_FLAG_EXT(-D_BSD_SOURCE)
AHX_CONFIG_FLAG_EXT(-D_DEFAULT_SOURCE)
AHX_CONFIG_FLAG_EXT(-D__EXTENSIONS__)
AHX_CONFIG_FLAG_EXT(-D_POSIX_C_SOURCE=200112)
AHX_CONFIG_FLAG_EXT(-D_XOPEN_SOURCE=600)
AHX_CONFIG_FLAG_EXT(-D_XOPEN_SOURCE_EXTENDED=1)
AHX_CONFIG_FLAG_EXT(-D_ALL_SOURCE)
AHX_CONFIG_FLAG_EXT(-D_LARGEFILE_SOURCE=1)
])

dnl check if memcmp is using signed characters and replace if so.
AC_DEFUN([ACX_CHECK_MEMCMP_SIGNED],
[AC_MSG_CHECKING([if memcmp compares unsigned])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
	char a = 255, b = 0;
	if(memcmp(&a, &b, 1) < 0)
		return 1;
	return 0;
}
]])], [AC_MSG_RESULT([yes]) ],
[ AC_MSG_RESULT([no])
  AC_DEFINE([MEMCMP_IS_BROKEN], [1], [Define if memcmp() does not compare unsigned bytes])
  AC_LIBOBJ([memcmp])
], [ AC_MSG_RESULT([cross-compile no])
  AC_DEFINE([MEMCMP_IS_BROKEN], [1], [Define if memcmp() does not compare unsigned bytes])
  AC_LIBOBJ([memcmp]) 
]) ])

dnl define memcmp to its replacement, pass unique id for program as arg
AC_DEFUN([AHX_MEMCMP_BROKEN], [
#ifdef MEMCMP_IS_BROKEN
#include "compat/memcmp.h"
#define memcmp memcmp_$1
int memcmp(const void *x, const void *y, size_t n);
#endif
])

dnl ACX_CHECK_SS_FAMILY           - check for sockaddr_storage.ss_family
AC_DEFUN([ACX_CHECK_SS_FAMILY],
[AC_CHECK_MEMBER([struct sockaddr_storage.ss_family], [], [
        AC_CHECK_MEMBER([struct sockaddr_storage.__ss_family], [
                AC_DEFINE([ss_family], [__ss_family], [Fallback member name for socket family in struct sockaddr_storage])
        ],, [AC_INCLUDES_DEFAULT
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
    ])
], [AC_INCLUDES_DEFAULT
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
]) ])

dnl Check if CC and linker support -fPIE and -pie.
dnl If so, sets them in CFLAGS / LDFLAGS.
AC_DEFUN([ACX_CHECK_PIE], [
    AC_ARG_ENABLE([pie], AS_HELP_STRING([--enable-pie], [Enable Position-Independent Executable (eg. to fully benefit from ASLR, small performance penalty)]))
    AS_IF([test "x$enable_pie" = "xyes"], [
	AC_MSG_CHECKING([if $CC supports PIE])
	BAKLDFLAGS="$LDFLAGS"
	BAKCFLAGS="$CFLAGS"
	LDFLAGS="$LDFLAGS -pie"
	CFLAGS="$CFLAGS -fPIE"
	AC_LINK_IFELSE([AC_LANG_PROGRAM([], [])], [
	    if $CC $CFLAGS $LDFLAGS -o conftest conftest.c 2>&1 | grep "warning: no debug symbols in executable" >/dev/null; then
		LDFLAGS="$BAKLDFLAGS"
		AC_MSG_RESULT(no)
	    else
		AC_MSG_RESULT(yes)
	    fi
	    rm -f conftest conftest.c conftest.o
	], [LDFLAGS="$BAKLDFLAGS" ; CFLAGS="$BAKCFLAGS" ; AC_MSG_RESULT(no)])
    ])
])

dnl Check if linker supports -Wl,-z,relro,-z,now.
dnl If so, adds it to LDFLAGS.
AC_DEFUN([ACX_CHECK_RELRO_NOW], [
    AC_ARG_ENABLE([relro_now], AS_HELP_STRING([--enable-relro-now], [Enable full relocation binding at load-time (RELRO NOW, to protect GOT and .dtor areas)]))
    AS_IF([test "x$enable_relro_now" = "xyes"], [
	AC_MSG_CHECKING([if $CC supports -Wl,-z,relro,-z,now])
	BAKLDFLAGS="$LDFLAGS"
	LDFLAGS="$LDFLAGS -Wl,-z,relro,-z,now"
	AC_LINK_IFELSE([AC_LANG_PROGRAM([], [])], [
	    if $CC $CFLAGS $LDFLAGS -o conftest conftest.c 2>&1 | grep "warning: no debug symbols in executable" >/dev/null; then
		LDFLAGS="$BAKLDFLAGS"
		AC_MSG_RESULT(no)
	    else
		AC_MSG_RESULT(yes)
	    fi
	    rm -f conftest conftest.c conftest.o
	], [LDFLAGS="$BAKLDFLAGS" ; AC_MSG_RESULT(no)])
    ])
])

dnl End of file
