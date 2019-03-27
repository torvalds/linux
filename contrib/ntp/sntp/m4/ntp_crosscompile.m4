dnl ######################################################################
AC_DEFUN([NTP_CROSSCOMPILE], [

# Expose a cross-compilation indicator to makefiles
AM_CONDITIONAL([NTP_CROSSCOMPILE], [test $build != $host])

])dnl
dnl ======================================================================
