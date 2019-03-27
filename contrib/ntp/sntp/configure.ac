dnl SNTP subpackage configure.ac			-*- Autoconf -*-
dnl
m4_include([m4/version.m4])
AC_PREREQ([2.61])
AC_INIT(
    [sntp],
    [VERSION_NUMBER],
    [http://bugs.ntp.org./],
    [],
    [http://www.ntp.org./]dnl
)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([libevent/build-aux])
AC_LANG([C])

# Bump sntp_configure_cache_version for each change to configure.ac or
# .m4 files which invalidates cached values from previous configure 
# runs.
#
# If the change affects cache variables used only by the main NTP
# configure.ac, then only its version number should be bumped, while
# the subdir configure.ac version numbers should be unchanged.  The
# same is true for a test/variable that is used only by one subdir
# being changed incompatibly; only that subdir's cache version needs
# bumping.
#
# If a change affects variables shared by all NTP configure scripts,
# please bump the version numbers of each.  If you are not sure, the
# safe choice is to bump all on any cache-invalidating change.
#
# In order to avoid the risk of version stamp collision between -stable
# and -dev branches, do not simply increment the version, instead use
# the date YYYYMMDD optionally with -HHMM if there is more than one
# bump in a day.

sntp_configure_cache_version=20120806

# When the version of config.cache and configure do not
# match, NTP_CACHEVERSION will flush the cache.

NTP_CACHEVERSION([sntp], [$sntp_configure_cache_version])

AM_INIT_AUTOMAKE([1.10 foreign -Wall -Wno-gnu])
dnl AM_SILENT_RULES req. automake 1.11.  [yes] defaults V=0
m4_ifdef(
    [AM_SILENT_RULES],
    [AM_SILENT_RULES([yes])]
)
AC_CANONICAL_BUILD
AC_CANONICAL_HOST
dnl the 'build' machine is where we run configure and compile
dnl the 'host' machine is where the resulting stuff runs.
AC_DEFINE_UNQUOTED([STR_SYSTEM], "$host", [canonical system (cpu-vendor-os) of where we should run])
AC_CONFIG_HEADER([config.h])
dnl AC_ARG_PROGRAM

NTP_PROG_CC
NTP_COMPILER
NTP_HARDEN

NTP_LOCINFO()

dnl AM_PROG_AR req. automake 1.12
m4_ifdef(
    [AM_PROG_AR],
    [AM_PROG_AR]
)

NTP_LIBNTP

AC_DISABLE_SHARED
AC_PROG_LIBTOOL
AC_SUBST([LIBTOOL_DEPS])

NTP_WITHSNTP

case "$SNTP" in
 '')
    SNTP_DB=
    SNTP_DL=
    SNTP_DS=
    ;;
esac

###

# NTP has (so far) been relying on leading-edge autogen.
# Therefore, by default:
# - use the version we ship with
# - do not install it
# - build a static copy (AC_DISABLE_SHARED - done earlier)
case "${enable_local_libopts+set}" in
 set) ;;
 *) enable_local_libopts=yes ;;
esac
case "${enable_libopts_install+set}" in
 set) ;;
 *) enable_libopts_install=no ;;
esac
enable_nls=no
LIBOPTS_CHECK

# From when we only used libevent for sntp:
#AM_COND_IF(
#    [BUILD_SNTP],
#    [NTP_LIBEVENT_CHECK],
#    [NTP_LIBEVENT_CHECK_NOBUILD]
#)

NTP_LIBEVENT_CHECK([2])

# Checks for libraries.

dnl NTP_LIBNTP checks for inet_XtoY
dnl AC_SEARCH_LIBS([inet_pton], [nsl])

dnl AC_SEARCH_LIBS([openlog], [gen syslog])
LIB_SYSLOG=''
AC_SUBST([LIB_SYSLOG])
HMS_SEARCH_LIBS([LIB_SYSLOG], [openlog], [gen syslog])

# Checks for header files.
AC_CHECK_HEADERS([netdb.h string.h strings.h syslog.h])
NTP_SYSEXITS_H
NTP_FACILITYNAMES

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL

NTP_OPENSSL
NTP_IPV6

###

# Hacks
# these need work if we're to move libntp under sntp
AC_DEFINE([HAVE_NO_NICE], 1, [sntp does not care about 'nice'])
AC_DEFINE([HAVE_TERMIOS], 1, [sntp does not care about TTY stuff])

# Checks for library functions.
AC_CHECK_FUNCS([socket])

NTP_UNITYBUILD
## HMS: if we don't find c++ we should not look for gtest.
#AC_PROG_CXX
#NTP_GOOGLETEST

SNTP_PROBLEM_TESTS

# All libraries should be in various LIB_* variables now. 
#LIBS=
# Sadly not.  There is a gettext() check somewhere, and on Solaris this pulls
# in -lintl -lgen, outside our "scope".

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([include/Makefile])
AC_CONFIG_FILES([scripts/Makefile])
AC_CONFIG_FILES([tests/Makefile])
AC_CONFIG_FILES([tests/fileHandlingTest.h])
AC_CONFIG_FILES([unity/Makefile])

AC_OUTPUT
