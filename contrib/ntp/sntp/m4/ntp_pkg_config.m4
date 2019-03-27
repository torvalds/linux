dnl NTP_PKG_CONFIG					-*- Autoconf -*-
dnl
dnl Look for pkg-config, which must be at least
dnl $ntp_pkgconfig_min_version.
dnl
AC_DEFUN([NTP_PKG_CONFIG], [

dnl lower the minimum version if you find an earlier one works
ntp_pkgconfig_min_version='0.15.0'
AC_PATH_TOOL([PKG_CONFIG], [pkg-config])
AS_UNSET([ac_cv_path_PKG_CONFIG])
AS_UNSET([ac_cv_path_ac_pt_PKG_CONFIG])

case "$PKG_CONFIG" in
 /*)
    AC_MSG_CHECKING([if pkg-config is at least version $ntp_pkgconfig_min_version])
    if $PKG_CONFIG --atleast-pkgconfig-version $ntp_pkgconfig_min_version; then
	AC_MSG_RESULT([yes])
    else
	AC_MSG_RESULT([no])
	PKG_CONFIG=""
    fi
    ;;
esac

]) dnl NTP_PKG_CONFIG

