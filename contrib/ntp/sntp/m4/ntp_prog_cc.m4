dnl ######################################################################
dnl NTP compiler basics
dnl
AC_DEFUN([NTP_PROG_CC], [
case "$build" in
 *-*-freebsd1?.*)
    cclist=cc
    ;;
 *)
    cclist="cc gcc"
    ;;
esac

dnl  we need to check for cross compile tools for vxWorks here

dnl must come before AC_PROG_CC or similar
#AC_USE_SYSTEM_EXTENSIONS

AC_PROG_CC([$cclist])

])dnl
dnl ======================================================================
