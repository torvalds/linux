dnl ######################################################################
dnl What directory path separator do we use?
AC_DEFUN([NTP_DIR_SEP], [
AC_CACHE_CHECK(
    [for directory path separator],
    [ntp_cv_dir_sep],
    [
	case "$ntp_cv_dir_sep" in
	 '')
	    case "$host_os" in
	     *djgpp | *mingw32* | *emx*)
		ntp_cv_dir_sep="'\\'"
		;;
	     *) 
		ntp_cv_dir_sep="'/'"
		;;
	    esac
	esac
    ]
)
AC_DEFINE_UNQUOTED([DIR_SEP], [$ntp_cv_dir_sep],
    [Directory separator character, usually / or \\])
])
dnl ======================================================================
