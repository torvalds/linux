dnl ######################################################################
dnl Check syslog.h for 'facilitynames' table
AC_DEFUN([NTP_FACILITYNAMES], [
AC_CACHE_CHECK([for facilitynames in syslog.h],ac_cv_HAVE_SYSLOG_FACILITYNAMES,[
AC_TRY_COMPILE([
#define SYSLOG_NAMES
#include <stdlib.h>
#include <syslog.h>
],
[ void *fnames; fnames = facilitynames; ],
ac_cv_HAVE_SYSLOG_FACILITYNAMES=yes,ac_cv_HAVE_SYSLOG_FACILITYNAMES=no,ac_cv_HAVE_SYSLOG_FACILITYNAMES=cross)])
case "$ac_cv_HAVE_SYSLOG_FACILITYNAMES" in
 yes)
    AC_DEFINE(HAVE_SYSLOG_FACILITYNAMES,1,[ ])
    ;;
 no)
    AC_MSG_WARN([No facilitynames in <syslog.h>])
    ;;
 cross)
    AC_MSG_WARN([facilitynames in <syslog.h> - cross-compiling])
    ;;
esac
])
dnl ======================================================================
