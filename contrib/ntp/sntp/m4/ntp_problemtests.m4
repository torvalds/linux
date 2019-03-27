dnl ######################################################################
dnl NTP_PROBLEM_TESTS
dnl
dnl Some platforms have problems building or running certain tests.
dnl While we're in the initial phase of the deployment of the test
dnl framework, sometimes we may need to disable these tests.
dnl
dnl This is where we do that.
dnl
AC_DEFUN([NTP_PROBLEM_TESTS], [
case "$build" in
 $host)	cross=0 ;;
 *)	cross=1 ;;
esac

AC_MSG_CHECKING([if we want to enable tests with undiagnosed problems])
AC_ARG_ENABLE(
    [problem-tests],
    [AS_HELP_STRING(
        [--enable-problem-tests],
        [+ enable tests with undiagnosed problems]
    )],
    [ntp_ept=$enableval],
    [ntp_ept=yes]
)
AC_MSG_RESULT([$ntp_ept])

AC_MSG_CHECKING([if we can run test-ntp_restrict])
ntp_test_ntp_restrict="no"
case "$ntp_ept:$cross:$host" in
 no:0:*-*-freebsd6.4) ;;
 no:0:*-*-hpux11.23*) ;;
 no:0:*-*-solaris*) ;;
 *) ntp_test_ntp_restrict="yes" ;;
esac
AC_MSG_RESULT([$ntp_test_ntp_restrict])
AM_CONDITIONAL([BUILD_TEST_NTP_RESTRICT], [test x$ntp_test_ntp_restrict = xyes])

AC_MSG_CHECKING([if we can run test-ntp_scanner])
ntp_test_ntp_scanner="no"
case "$ntp_ept:$cross:$host" in
 no:0:*-*-freebsd6.4) ;;
 no:0:*-*-solaris*) ;;
 *) ntp_test_ntp_scanner="yes" ;;
esac
AC_MSG_RESULT([$ntp_test_ntp_scanner])
AM_CONDITIONAL([BUILD_TEST_NTP_SCANNER], [test x$ntp_test_ntp_scanner = xyes])

AC_MSG_CHECKING([if we can run test-ntp_signd])
ntp_test_ntp_signd="no"
case "$ntp_ept:$cross:$host" in
 no:0:*-*-freebsd6.4) ;;
 no:0:*-*-solaris*) ;;
 *) ntp_test_ntp_signd="yes" ;;
esac
AC_MSG_RESULT([$ntp_test_ntp_signd])
AM_CONDITIONAL([BUILD_TEST_NTP_SIGND], [test x$ntp_test_ntp_signd = xyes])

])
dnl ======================================================================
