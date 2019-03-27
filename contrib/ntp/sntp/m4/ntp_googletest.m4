dnl ######################################################################
dnl NTP_GOOGLETEST gtest support shared by top-level and sntp/configure.ac
AC_DEFUN([NTP_GOOGLETEST], [
gta=false
AC_ARG_WITH(
    [gtest],
    [AS_HELP_STRING(
	[--with-gtest],
	[Use the gtest framework (Default: if it's available)]
    )],
    [try_gtest=$withval],
    [try_gtest=yes]
)
case "$try_gtest" in
 yes)
    AC_PATH_PROG([GTEST_CONFIG], [gtest-config])
    AS_UNSET([ac_cv_path_GTEST_CONFIG])
    case x${GTEST_CONFIG} in
     x) ;;
     *)
	AC_MSG_CHECKING([gtest version])
	gtest_version_test=`$GTEST_CONFIG --min-version=1.5 || echo toolow`
	case "$gtest_version_test" in
	 toolow*)
	    ;;
	 *)
	    GTEST_LDFLAGS=`$GTEST_CONFIG --ldflags`
	    GTEST_LIBS=`$GTEST_CONFIG --libs`
	    GTEST_CXXFLAGS=`$GTEST_CONFIG --cxxflags`
	    GTEST_CPPFLAGS=`$GTEST_CONFIG --cppflags`
	    AC_SUBST([GTEST_LDFLAGS])
	    AC_SUBST([GTEST_LIBS])
	    AC_SUBST([GTEST_CXXFLAGS])
	    AC_SUBST([GTEST_CPPFLAGS])
	    gta=true
	    ;;
	esac
	gtest_version=`$GTEST_CONFIG --version`
	case "$gta" in
	 true)
	    AC_MSG_RESULT([($gtest_version) ok])
	    ;;
	 *) AC_MSG_RESULT([($gtest_version) not ok])
	    ;;
	esac
	AS_UNSET([gtest_version_test])
	AS_UNSET([gtest_version])
    esac
esac
AM_CONDITIONAL([GTEST_AVAILABLE], [$gta])

])
dnl ======================================================================
