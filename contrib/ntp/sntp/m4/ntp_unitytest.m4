dnl ######################################################################
dnl NTP_UNITYBUILD -  Unity build support
dnl shared by top-level and sntp/configure.ac
AC_DEFUN([NTP_UNITYBUILD], [
# We may not need have_unity
have_unity=false
AC_PATH_PROG([PATH_RUBY], [ruby])
case "$PATH_RUBY" in
 /*)
    have_unity=true
    ;;
 *) PATH_RUBY="false"
    ;;
esac
# We may not need UNITYBUILD_AVAILABLE
AM_CONDITIONAL([UNITYBUILD_AVAILABLE], [$have_unity])

])
dnl ======================================================================
