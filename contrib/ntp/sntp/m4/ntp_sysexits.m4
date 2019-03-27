dnl ######################################################################
dnl sysexits.h
AC_DEFUN([NTP_SYSEXITS_H], [

AC_CHECK_HEADERS([sysexits.h], [],[
AC_DEFINE([EX_OK], [0], [successful termination])
AC_DEFINE([EX_SOFTWARE], [70], [internal software error])
])dnl

])dnl
dnl ======================================================================
