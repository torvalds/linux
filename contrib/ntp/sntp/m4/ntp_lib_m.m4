dnl ######################################################################
dnl What do we need for math libraries?
AC_DEFUN([NTP_LIB_M], [
LIBM=
case "$host" in
 *-*-darwin*)
    ;;
 *)
    _libs=$LIBS
    AC_SEARCH_LIBS([cos], [m], [LIBM="-lm"])
    LIBS=$_libs
    ;;
esac
AC_SUBST([LIBM])
AS_UNSET([_libs])
])
dnl ======================================================================
