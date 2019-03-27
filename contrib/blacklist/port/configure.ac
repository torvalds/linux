dnl Process this file with autoconf to produce a configure script.
AC_INIT([blacklistd],[0.1],[christos@netbsd.com])
AM_INIT_AUTOMAKE([subdir-objects foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_SUBST(WARNINGS)

dnl Checks for programs.
AC_PROG_CC_STDC
AC_USE_SYSTEM_EXTENSIONS
AM_PROG_CC_C_O
AC_C_BIGENDIAN
AC_PROG_INSTALL
AC_PROG_LN_S
LT_INIT([disable-static pic-only])
gl_VISIBILITY
dnl Checks for headers
AC_HEADER_STDC
AC_HEADER_MAJOR
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS(stdint.h fcntl.h stdint.h inttypes.h unistd.h)
AC_CHECK_HEADERS(sys/un.h sys/socket.h limits.h)
AC_CHECK_HEADERS(arpa/inet.h getopt.h err.h)
AC_CHECK_HEADERS(sys/types.h util.h sys/time.h time.h)
AC_CHECK_HEADERS(netatalk/at.h net/if_dl.h db.h db_185.h)
AC_CHECK_LIB(rt, clock_gettime)
AC_CHECK_LIB(db, __db185_open)
AC_CHECK_LIB(util, pidfile)
AC_CHECK_LIB(util, sockaddr_snprintf)

AH_BOTTOM([
#ifndef __NetBSD__
#include "port.h"
#endif
])

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_SYS_LARGEFILE
AC_CHECK_MEMBERS([struct sockaddr.sa_len], [], [], [#include <sys/socket.h>])

AC_TYPE_PID_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_INT32_T
AC_TYPE_UINT64_T
AC_TYPE_INT64_T
AC_TYPE_INTPTR_T
AC_TYPE_UINTPTR_T

AC_MSG_CHECKING(for gcc compiler warnings)
AC_ARG_ENABLE(warnings,
[  --disable-warnings	disable compiler warnings],
[if test "${enableval}" = no -o "$GCC" = no; then
   AC_MSG_RESULT(no)
   WARNINGS=
else
   AC_MSG_RESULT(yes)
   WARNINGS="-Wall -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith \
       -Wmissing-declarations -Wredundant-decls -Wnested-externs \
       -Wsign-compare -Wreturn-type -Wswitch -Wshadow \
       -Wcast-qual -Wwrite-strings -Wextra -Wunused-parameter -Wformat=2"
fi], [
if test "$GCC" = yes; then
   AC_MSG_RESULT(yes)
   WARNINGS="-Wall -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith \
       -Wmissing-declarations -Wredundant-decls -Wnested-externs \
       -Wsign-compare -Wreturn-type -Wswitch -Wshadow \
       -Wcast-qual -Wwrite-strings -Wextra -Wunused-parameter -Wformat=2"
else
   WARNINGS=
   AC_MSG_RESULT(no)
fi])

dnl Checks for functions
AC_CHECK_FUNCS(strerror)

dnl Provide implementation of some required functions if necessary
AC_REPLACE_FUNCS(strtoi sockaddr_snprintf popenve clock_gettime strlcpy strlcat getprogname fparseln fgetln pidfile)

dnl See if we are cross-compiling
AM_CONDITIONAL(IS_CROSS_COMPILE, test "$cross_compiling" = yes)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
