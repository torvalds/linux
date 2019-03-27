dnl ######################################################################
dnl @synopsis NTP_LOCINFO([SCRIPTSDIRPATH])
dnl Location information:
dnl - installation directory (*_DB for bin/, *_DL for libexec/, *_DS for sbin/)
dnl   ... or *_NI for noinst_*
dnl - man tag format (man or mdoc)
dnl - man section (1, 1m, 1M, 8)

AC_DEFUN([NTP_LOCINFO], [

AC_MSG_CHECKING([for install dir and man conventions])

AC_ARG_WITH(
    [locfile],
    [AS_HELP_STRING(
	[--with-locfile=XXX],
	[os-specific or "legacy"]
    )],
    [],
    [with_locfile=no]
)

(									\
    SENTINEL_DIR="$PWD" &&						\
    cd $srcdir/$1 &&							\
    case "$with_locfile" in						\
     yes|no|'')								\
	scripts/genLocInfo -d "$SENTINEL_DIR"				\
	;;								\
     *)									\
	scripts/genLocInfo -d "$SENTINEL_DIR" -f "$with_locfile"	\
	;;								\
    esac								\
) > genLocInfo.i 2> genLocInfo.err
. ./genLocInfo.i

case "$GENLOCINFO" in
 OK)
    AC_MSG_RESULT([in file $GENLOCINFOFILE])
    rm genLocInfo.err genLocInfo.i
    ;;
 *)
    AC_MSG_RESULT([failed.])
    AC_MSG_ERROR([Problem with genLocInfo!])
    ;;
esac

AM_CONDITIONAL([WANT_CALC_TICKADJ_MS], [test "$CALC_TICKADJ_MS" ])

AC_SUBST([CALC_TICKADJ_DB])
AC_SUBST([CALC_TICKADJ_DL])
AC_SUBST([CALC_TICKADJ_DS])
AC_SUBST([CALC_TICKADJ_MS])
AC_SUBST([CALC_TICKADJ_NI])
AC_SUBST([MANTAGFMT])
AC_SUBST([NTPDATE_DB])
AC_SUBST([NTPDATE_DL])
AC_SUBST([NTPDATE_DS])
AC_SUBST([NTPDATE_MS])
AC_SUBST([NTPDATE_NI])
AC_SUBST([NTPDC_DB])
AC_SUBST([NTPDC_DL])
AC_SUBST([NTPDC_DS])
AC_SUBST([NTPDC_MS])
AC_SUBST([NTPDC_NI])
AC_SUBST([NTPDSIM_DB])
AC_SUBST([NTPDSIM_DL])
AC_SUBST([NTPDSIM_DS])
AC_SUBST([NTPDSIM_MS])
AC_SUBST([NTPDSIM_NI])
AC_SUBST([NTPD_DB])
AC_SUBST([NTPD_DL])
AC_SUBST([NTPD_DS])
AC_SUBST([NTPD_MS])
AC_SUBST([NTPD_NI])
AC_SUBST([NTPQ_DB])
AC_SUBST([NTPQ_DL])
AC_SUBST([NTPQ_DS])
AC_SUBST([NTPQ_MS])
AC_SUBST([NTPQ_NI])
AC_SUBST([NTPSNMPD_DB])
AC_SUBST([NTPSNMPD_DL])
AC_SUBST([NTPSNMPD_DS])
AC_SUBST([NTPSNMPD_MS])
AC_SUBST([NTPSNMPD_NI])
AC_SUBST([NTPSWEEP_DB])
AC_SUBST([NTPSWEEP_DL])
AC_SUBST([NTPSWEEP_DS])
AC_SUBST([NTPSWEEP_MS])
AC_SUBST([NTPSWEEP_NI])
AM_CONDITIONAL([INSTALL_NTPSWEEP], [test -z "$NTPSWEEP_NI" ])
AC_SUBST([NTPTIME_DB])
AC_SUBST([NTPTIME_DL])
AC_SUBST([NTPTIME_DS])
AC_SUBST([NTPTIME_MS])
AC_SUBST([NTPTIME_NI])
AC_SUBST([NTPTRACE_DB])
AC_SUBST([NTPTRACE_DL])
AC_SUBST([NTPTRACE_DS])
AC_SUBST([NTPTRACE_MS])
AC_SUBST([NTPTRACE_NI])
AC_SUBST([NTP_KEYGEN_DB])
AC_SUBST([NTP_KEYGEN_DL])
AC_SUBST([NTP_KEYGEN_DS])
AC_SUBST([NTP_KEYGEN_MS])
AC_SUBST([NTP_KEYGEN_NI])
AC_SUBST([NTP_WAIT_DB])
AC_SUBST([NTP_WAIT_DL])
AC_SUBST([NTP_WAIT_DS])
AC_SUBST([NTP_WAIT_MS])
AC_SUBST([NTP_WAIT_NI])
AC_SUBST([SNTP_DB])
AC_SUBST([SNTP_DL])
AC_SUBST([SNTP_DS])
AC_SUBST([SNTP_MS])
AC_SUBST([SNTP_NI])
AC_SUBST([TICKADJ_DB])
AC_SUBST([TICKADJ_DL])
AC_SUBST([TICKADJ_DS])
AC_SUBST([TICKADJ_MS])
AC_SUBST([TICKADJ_NI])
AC_SUBST([TIMETRIM_DB])
AC_SUBST([TIMETRIM_DL])
AC_SUBST([TIMETRIM_DS])
AC_SUBST([TIMETRIM_MS])
AC_SUBST([TIMETRIM_NI])
AC_SUBST([UPDATE_LEAP_DB])
AC_SUBST([UPDATE_LEAP_DL])
AC_SUBST([UPDATE_LEAP_DS])
AC_SUBST([UPDATE_LEAP_MS])
AC_SUBST([UPDATE_LEAP_NI])
AM_CONDITIONAL([INSTALL_UPDATE_LEAP], [test -z "$UPDATE_LEAP_NI" ])

])dnl
dnl ======================================================================
