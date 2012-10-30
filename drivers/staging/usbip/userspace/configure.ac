dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([usbip-utils], [1.1.1], [linux-usb@vger.kernel.org])
AC_DEFINE([USBIP_VERSION], [0x00000111], [binary-coded decimal version number])

CURRENT=0
REVISION=1
AGE=0
AC_SUBST([LIBUSBIP_VERSION], [$CURRENT:$REVISION:$AGE], [library version])

AC_CONFIG_SRCDIR([src/usbipd.c])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([foreign])
LT_INIT

# Silent build for automake >= 1.11
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_SUBST([EXTRA_CFLAGS], ["-Wall -Werror -Wextra -std=gnu99"])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netdb.h netinet/in.h stdint.h stdlib.h dnl
		  string.h sys/socket.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT32_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memset mkdir regcomp socket strchr strerror strstr dnl
		strtoul])

AC_CHECK_HEADER([sysfs/libsysfs.h],
		[AC_CHECK_LIB([sysfs], [sysfs_open_directory_list],
			      [LIBS="$LIBS -lsysfs"],
			      [AC_MSG_ERROR([Missing sysfs2 library!])])],
		[AC_MSG_ERROR([Missing /usr/include/sysfs/libsysfs.h])])

# Checks for libwrap library.
AC_MSG_CHECKING([whether to use the libwrap (TCP wrappers) library])
AC_ARG_WITH([tcp-wrappers],
	    [AS_HELP_STRING([--with-tcp-wrappers],
			    [use the libwrap (TCP wrappers) library])],
	    dnl [ACTION-IF-GIVEN]
	    [if test "$withval" = "yes"; then
		     AC_MSG_RESULT([yes])
		     AC_MSG_CHECKING([for hosts_access in -lwrap])
		     saved_LIBS="$LIBS"
		     LIBS="-lwrap $saved_LIBS"
		     AC_TRY_LINK(
		       [int hosts_access(); int allow_severity, deny_severity;],
		       [hosts_access()],
		       [AC_MSG_RESULT([yes]);
			AC_DEFINE([HAVE_LIBWRAP], [1],
				  [use tcp wrapper]) wrap_LIB="-lwrap"],
		       [AC_MSG_RESULT([not found]); exit 1])
	     else
		     AC_MSG_RESULT([no]);
		     LIBS="$saved_LIBS"
	     fi],
	    dnl [ACTION-IF-NOT-GIVEN]
	    [AC_MSG_RESULT([(default)])
	     AC_MSG_CHECKING([for hosts_access in -lwrap])
	     saved_LIBS="$LIBS"
	     LIBS="-lwrap $saved_LIBS"
	     AC_TRY_LINK(
	       [int hosts_access(); int allow_severity, deny_severity;],
	       [hosts_access()],
	       [AC_MSG_RESULT([yes]);
		AC_DEFINE([HAVE_LIBWRAP], [1], [use tcp wrapper])],
	       [AC_MSG_RESULT([no]); LIBS="$saved_LIBS"])])

# Sets directory containing usb.ids.
AC_ARG_WITH([usbids-dir],
	    [AS_HELP_STRING([--with-usbids-dir=DIR],
	       [where usb.ids is found (default /usr/share/hwdata/)])],
	    [USBIDS_DIR=$withval], [USBIDS_DIR="/usr/share/hwdata/"])
AC_SUBST([USBIDS_DIR])

GLIB2_REQUIRED=2.6.0
PKG_CHECK_MODULES([PACKAGE], [glib-2.0 >= $GLIB2_REQUIRED])
AC_SUBST([PACKAGE_CFLAGS])
AC_SUBST([PACKAGE_LIBS])

AC_CONFIG_FILES([Makefile libsrc/Makefile src/Makefile])
AC_OUTPUT
