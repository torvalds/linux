#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_REVISION($Id$)
AC_INIT(usbip, 0.1.8, usbip-devel@lists.sourceforge.net)
AC_DEFINE(USBIP_VERSION, 0x000106, Numeric version number)

AC_CONFIG_SRCDIR([config.h.in])
AM_INIT_AUTOMAKE([foreign])

# Silent build for automake >= 1.11
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AM_CONFIG_HEADER(config.h)


AC_SUBST([EXTRA_CFLAGS])

AC_PROG_LIBTOOL

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# Checks for libraries.

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h netdb.h stdint.h stdlib.h string.h strings.h sys/socket.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([bzero memset strchr strerror strstr strtoul])

AC_CHECK_HEADER(sysfs/libsysfs.h,
	[AC_CHECK_LIB(sysfs, sysfs_open_directory_list,
		[LIBS="$LIBS -lsysfs"],
		[AC_MSG_ERROR([Missing sysfs2 library!])])],
	[AC_MSG_ERROR([Missing /usr/include/sysfs/libsysfs.h])])


# get from stunnel
dnl Check for libwrap library.
AC_MSG_CHECKING([whether to use the libwrap (TCP wrappers) library])

AC_ARG_WITH(tcp-wrappers,
[  --with-tcp-wrappers     Use the libwrap (TCP wrappers) library ],
[saved_LIBS="$LIBS"
 if test "$withval" = "yes"; then
      AC_MSG_RESULT([yes])
      AC_MSG_CHECKING([for hosts_access in -lwrap])
      LIBS="-lwrap $LIBS"
      AC_TRY_LINK(
	[int hosts_access(); int allow_severity, deny_severity;],
	[hosts_access()],
	[AC_MSG_RESULT([yes]); AC_DEFINE(HAVE_LIBWRAP, 1, use tcp wrapper) wrap_LIB="-lwrap"],
	[AC_MSG_RESULT([not found]); exit 1])
 else
 	     AC_MSG_RESULT([no])
 fi
 LIBS="$saved_LIBS"],
[AC_MSG_RESULT([(default)])
 AC_MSG_CHECKING([for hosts_access in -lwrap])
 saved_LIBS="$LIBS"
 LIBS="-lwrap $saved_LIBS"
 AC_TRY_LINK(
	[int hosts_access(); int allow_severity, deny_severity;],
 	[hosts_access()],
 	[AC_MSG_RESULT([yes]); AC_DEFINE(HAVE_LIBWRAP, 1, use tcp wrapper)],
 	[AC_MSG_RESULT([no]); LIBS="$saved_LIBS"])
])



USBIDS_DIR='${datadir}/usbip'
AC_ARG_WITH(usbids-dir, [  --with-usbids-dir=DIR   where usb.ids is found (default ${datadir}/usbip)], [USBIDS_DIR=$withval])
AC_SUBST(USBIDS_DIR)

dnl FIXME: when disabled, empty directry is created
usbids=install
AC_ARG_ENABLE(usbids-install,
[  --enable-usbids-install     install usb.ids (default)],
[case "${enableval}" in
 yes) usbids=install ;;
 no)  usbids=notinstall ;;
 *)   AC_MSG_ERROR(bad value ${enableval} for --enable-usbids-install) ;;
 esac])
AM_CONDITIONAL(INSTALL_USBIDS, test x$usbids = xinstall)
    	


GLIB2_REQUIRED=2.6.0
PKG_CHECK_MODULES(PACKAGE, glib-2.0 >= $GLIB2_REQUIRED)
AC_SUBST(PACKAGE_CFLAGS)
AC_SUBST(PACKAGE_LIBS)

AC_CONFIG_FILES([Makefile lib/Makefile cmd/Makefile])
AC_OUTPUT
