#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.56)
AC_INIT(ldns, 1.7.0, libdns@nlnetlabs.nl,libdns)
AC_CONFIG_SRCDIR([drill.c])
sinclude(../acx_nlnetlabs.m4)

OURCPPFLAGS=''
CPPFLAGS=${CPPFLAGS:-${OURCPPFLAGS}}
OURCFLAGS='-g'
CFLAGS=${CFLAGS:-${OURCFLAGS}}
AC_DEFINE(WINVER, 0x0502, [the version of the windows API enabled])

AC_AIX
# Checks for programs.
AC_PROG_CC
AC_PROG_MAKE_SET
AC_CHECK_PROGS(libtool, [glibtool libtool15 libtool], [../libtool])

# add option to disable the evil rpath
dnl Check whether to use rpath or not
AC_ARG_ENABLE(rpath,
        [  --disable-rpath                disable hardcoded rpath (default=enabled)],
	enable_rpath=$enableval, enable_rpath=yes)

if test "x$enable_rpath" = xyes; then
    RPATH_VAL="-Wl,-rpath=\${libdir}"
fi


ACX_CHECK_COMPILER_FLAG(std=c99, [C99FLAG="-std=c99"])
ACX_CHECK_COMPILER_FLAG(xc99, [C99FLAG="-xc99"])

AC_TYPE_SIZE_T
ACX_CHECK_COMPILER_FLAG(O2, [CFLAGS="$CFLAGS -O2"])

ACX_CHECK_COMPILER_FLAG_NEEDED($C99FLAG -D__EXTENSIONS__ -D_BSD_SOURCE -D_POSIX_C_SOURCE=200112 -D_XOPEN_SOURCE=600,
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
	char *t;
	time_t time = 0;
	char *buf = NULL;
	t = ctime_r(&time, buf);
	tv.tv_usec = 10;
	srandom(32);
	a = getopt(2, opts, "a");
	a = isascii(32);
	return a;
}
], [CFLAGS="$CFLAGS $C99FLAG -D__EXTENSIONS__ -D_BSD_SOURCE -D_POSIX_C_SOURCE=200112 -D_XOPEN_SOURCE=600"])


ACX_CHECK_COMPILER_FLAG_NEEDED($C99FLAG, [#include <stdbool.h>], [CFLAGS="$CFLAGS $C99FLAG"])

AC_C_INLINE
AC_CHECK_TYPE(int8_t, char)
AC_CHECK_TYPE(int16_t, short)
AC_CHECK_TYPE(int32_t, int)
AC_CHECK_TYPE(int64_t, long long)
AC_CHECK_TYPE(uint8_t, unsigned char)
AC_CHECK_TYPE(uint16_t, unsigned short)
AC_CHECK_TYPE(uint32_t, unsigned int)
AC_CHECK_TYPE(uint64_t, unsigned long long)
AC_CHECK_TYPE(ssize_t, int)

AC_CHECK_HEADERS([sys/types.h getopt.h stdlib.h stdio.h assert.h netinet/in.h ctype.h time.h arpa/inet.h sys/time.h sys/socket.h sys/select.h],,, [AC_INCLUDES_DEFAULT])
AC_CHECK_HEADERS([netinet/in_systm.h net/if.h netinet/ip.h netinet/udp.h netinet/if_ether.h netinet/ip6.h],,, [
AC_INCLUDES_DEFAULT 
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif])
# MinGW32 tests
AC_CHECK_HEADERS([winsock2.h ws2tcpip.h],,, [AC_INCLUDES_DEFAULT])

ACX_TYPE_SOCKLEN_T
AC_CHECK_HEADERS([sys/param.h sys/mount.h],,,
[AC_INCLUDES_DEFAULT]
[
  [
   #if HAVE_SYS_PARAM_H
   # include <sys/param.h>
   #endif
  ]
])
AC_CHECK_TYPE(in_addr_t, [], [AC_DEFINE([in_addr_t], [uint32_t], [in_addr_t])], [
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif])
AC_CHECK_TYPE(in_port_t, [], [AC_DEFINE([in_port_t], [uint16_t], [in_port_t])], [
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif])

# check to see if libraries are needed for these functions.
AC_SEARCH_LIBS(socket, socket)
AC_SEARCH_LIBS([inet_pton], [nsl])

ACX_WITH_SSL_OPTIONAL

ACX_CHECK_GETADDRINFO_WITH_INCLUDES

LIBS_STC="$LIBS"
AC_SUBST(LIBS_STC)

# check for ldns
AC_ARG_WITH(ldns, 
	AC_HELP_STRING([--with-ldns=PATH        specify prefix of path of ldns library to use])
	,
	[
		specialldnsdir="$withval"
		CPPFLAGS="$CPPFLAGS -I$withval/include"
		LDFLAGS="-L$withval -L$withval/lib $LDFLAGS"
		LDNSDIR="$withval"
		LIBS="-lldns $LIBS"
		LIBS_STC="$withval/lib/libldns.a $LIBS_STC"
	]
)

#AC_CHECK_HEADER(ldns/ldns.h,,  [
#	AC_MSG_ERROR([Can't find ldns headers (make copy-headers in devel source.)])
#	], [AC_INCLUDES_DEFAULT]
#)

AC_CHECK_FUNCS(isblank)

# check for ldns development source tree
AC_MSG_CHECKING([for ldns devel source])
ldns_dev_dir=..
if test -f $ldns_dev_dir/ldns/util.h && \
	grep LDNS_VERSION $ldns_dev_dir/ldns/util.h >/dev/null; then
	ldns_version=`grep LDNS_VERSION $ldns_dev_dir/ldns/util.h | sed -e 's/^.*"\(.*\)".*$/\1/'`
	AC_MSG_RESULT([using $ldns_dev_dir with $ldns_version])
	CPPFLAGS="$CPPFLAGS -I$ldns_dev_dir/include"
	LDFLAGS="-L$ldns_dev_dir -L$ldns_dev_dir/lib $LDFLAGS"
	LIBS="-lldns $LIBS"
	AC_DEFINE(HAVE_LIBLDNS, 1, [If the ldns library is available.])
	LDNSDIR="$ldns_dev_dir"
	LIBS_STC="$ldns_dev_dir/lib/libldns.a $LIBS_STC"
else
	AC_MSG_RESULT([no])
	AC_CHECK_LIB(ldns, ldns_rr_new, , [
			AC_MSG_ERROR([Can't find ldns library])dnl '
		]
	)
fi

AC_SUBST(LDNSDIR)

AC_ARG_WITH(trust-anchor, AC_HELP_STRING([--with-trust-anchor=KEYFILE],
[Default location of the trust anchor file. [default=SYSCONFDIR/unbound/root.key]]), [
 LDNS_TRUST_ANCHOR_FILE="$withval"
],[
 if test "x$LDNS_TRUST_ANCHOR_FILE" = "x"; then
  if test "x$sysconfdir" = 'x${prefix}/etc' ; then
   if test "x$prefix" = 'xNONE' ; then
    LDNS_TRUST_ANCHOR_FILE="/etc/unbound/root.key"
   else
    LDNS_TRUST_ANCHOR_FILE="${prefix}/etc/unbound/root.key"
   fi
  else
    LDNS_TRUST_ANCHOR_FILE="${sysconfdir}/unbound/root.key"
  fi
 fi
])
AC_DEFINE_UNQUOTED([LDNS_TRUST_ANCHOR_FILE], ["$LDNS_TRUST_ANCHOR_FILE"], [Default trust anchor file])
AC_SUBST(LDNS_TRUST_ANCHOR_FILE)
AC_MSG_NOTICE([Default trust anchor: $LDNS_TRUST_ANCHOR_FILE])

AH_BOTTOM([

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
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

#ifdef HAVE_NETINET_UDP_H
#include <netinet/udp.h>
#endif

#ifdef HAVE_TIME_H
#include <time.h>
#endif

#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif

#ifdef HAVE_NETINET_IP_H
#include <netinet/ip.h>
#endif

#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif

#ifdef HAVE_NETINET_IF_ETHER_H
#include <netinet/if_ether.h>
#endif

#ifdef HAVE_WINSOCK2_H
#define USE_WINSOCK 1
#include <winsock2.h>
#endif

#ifdef HAVE_WS2TCPIP_H
#include <ws2tcpip.h>
#endif

#ifndef EXIT_FAILURE
#define EXIT_FAILURE  1
#endif
#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS  0
#endif

#ifdef S_SPLINT_S
#define FD_ZERO(a) /* a */
#define FD_SET(a,b) /* a, b */
#endif
])

AC_CONFIG_FILES([Makefile drill.1])
AC_CONFIG_HEADER([config.h])
AC_OUTPUT
