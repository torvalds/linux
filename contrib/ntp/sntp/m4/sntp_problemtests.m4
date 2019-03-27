dnl ######################################################################
dnl SNTP_PROBLEM_TESTS
dnl
dnl Some platforms have problems building or running certain tests.
dnl While we're in the initial phase of the deployment of the test
dnl framework, sometimes we may need to disable these tests.
dnl
dnl This is where we do that.
dnl
AC_DEFUN([SNTP_PROBLEM_TESTS], [
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
    [sntp_ept=$enableval],
    [sntp_ept=yes]
)
AC_MSG_RESULT([$sntp_ept])

AC_MSG_CHECKING([if we can run test-kodDatabase])
sntp_test_kodDatabase="no"
case "$sntp_ept:$cross:$host" in
 no:0:*-apple-darwin12.6.0) ;;
 *) sntp_test_kodDatabase="yes" ;;
esac
AC_MSG_RESULT([$sntp_test_kodDatabase])
AM_CONDITIONAL([BUILD_TEST_KODDATABASE], [test x$sntp_test_kodDatabase = xyes])

AC_MSG_CHECKING([if we can run test-kodFile])
sntp_test_kodFile="no"
case "$sntp_ept:$cross:$host" in
 no:0:*-apple-darwin12.6.0) ;;
 *) sntp_test_kodFile="yes" ;;
esac
AC_MSG_RESULT([$sntp_test_kodFile])
AM_CONDITIONAL([BUILD_TEST_KODFILE], [test x$sntp_test_kodFile = xyes])

])
dnl ======================================================================
