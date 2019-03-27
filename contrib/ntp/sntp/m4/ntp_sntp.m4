dnl ######################################################################
dnl Common m4sh code SNTP
AC_DEFUN([NTP_WITHSNTP], [

dnl preset withsntp=no in env to change default to --without-sntp
AC_MSG_CHECKING([if sntp will be built])
AC_ARG_WITH(
    [sntp],
    [AS_HELP_STRING(
	[--without-sntp],
	[- disable building sntp and sntp/tests]
    )],
    [],
    [with_sntp="${withsntp=yes}"]
)
case "$with_sntp" in
 no)
    SNTP=
    ;;
 *)
    SNTP=sntp
    ;;
esac
AC_SUBST([SNTP])
AM_CONDITIONAL([BUILD_SNTP], [test -n "$SNTP"])
AC_MSG_RESULT([$with_sntp])

])dnl
dnl ======================================================================
