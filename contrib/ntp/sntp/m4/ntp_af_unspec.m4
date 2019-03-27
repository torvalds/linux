dnl ######################################################################
dnl AF_UNSPEC checks
AC_DEFUN([NTP_AF_UNSPEC], [

# We could do a cv check here, but is it worth it?

AC_COMPILE_IFELSE(
  [AC_LANG_PROGRAM(
    [[
    	#include <sys/socket.h>
	#ifndef AF_UNSPEC
	#include "Bletch: AF_UNSPEC is undefined!"
	#endif
	#if AF_UNSPEC != 0
	#include "Bletch: AF_UNSPEC != 0"
	#endif
    ]],
    [AC_MSG_NOTICE([AF_UNSPEC is zero, as expected.])],
    [AC_MSG_ERROR([AF_UNSPEC is not zero on this platform!])]
  )]
)])

dnl ######################################################################
