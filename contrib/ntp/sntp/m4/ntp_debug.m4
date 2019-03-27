dnl ######################################################################
dnl Common m4sh code for debug
AC_DEFUN([NTP_DEBUG], [

AC_MSG_CHECKING([if we're including debugging code])
AC_ARG_ENABLE(
    [debugging],
    [AS_HELP_STRING(
        [--enable-debugging],
        [+ include ntpd debugging code]
    )],
    [ntp_ok=$enableval],
    [ntp_ok=yes]
)
case "$ntp_ok" in
 yes)
    AC_DEFINE([DEBUG], [1], [Enable debugging code?])
esac
AC_MSG_RESULT([$ntp_ok])

])dnl
dnl ======================================================================
