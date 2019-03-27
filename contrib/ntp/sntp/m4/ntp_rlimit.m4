dnl ######################################################################
dnl rlimit capabilities checks
AC_DEFUN([NTP_RLIMIT_ITEMS], [

AC_CACHE_CHECK(
    [for RLIMIT_MEMLOCK],
    [ntp_cv_rlimit_memlock],
    [AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM(
	    [[
		#ifdef HAVE_SYS_TYPES_H
		# include <sys/types.h>
		#endif
		#ifdef HAVE_SYS_TIME_H
		# include <sys/time.h>
		#endif
		#ifdef HAVE_SYS_RESOURCE_H
		# include <sys/resource.h>
		#endif
	    ]],
	    [[
		getrlimit(RLIMIT_MEMLOCK, 0);
	    ]]
	)],
	[ntp_cv_rlimit_memlock=yes],
	[ntp_cv_rlimit_memlock=no]
    )]
)
case "$host" in
 *-*-*linux*)
    ntp_dflt_rlimit_memlock="-1" ;;
 *) ntp_dflt_rlimit_memlock="32" ;;
esac
case "$ntp_cv_rlimit_memlock" in
 yes)
    AC_SUBST([HAVE_RLIMIT_MEMLOCK])
    HAVE_RLIMIT_MEMLOCK=" memlock $ntp_dflt_rlimit_memlock"  ;;
esac

AC_CACHE_CHECK(
    [for RLIMIT_STACK],
    [ntp_cv_rlimit_stack],
    [AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM(
	    [[
		#ifdef HAVE_SYS_TYPES_H
		# include <sys/types.h>
		#endif
		#ifdef HAVE_SYS_TIME_H
		# include <sys/time.h>
		#endif
		#ifdef HAVE_SYS_RESOURCE_H
		# include <sys/resource.h>
		#endif
	    ]],
	    [[
		getrlimit(RLIMIT_STACK, 0);
	    ]]
	)],
	[ntp_cv_rlimit_stack=yes],
	[ntp_cv_rlimit_stack=no]
    )]
)
case "$ntp_cv_rlimit_stack" in
 yes)
    AC_SUBST([HAVE_RLIMIT_STACK])
    HAVE_RLIMIT_STACK=" stacksize 50"
esac

# HMS: Only if we are doing the MLOCKALL stuff...
AC_MSG_CHECKING([for the default number of 4k stack pages])
AC_ARG_WITH(
    [stack-limit],
    [AS_HELP_STRING(
	[--with-stack-limit],
	[? =50 (200 for openbsd) 4k pages]
    )],
    [ans=$withval],
    [ans=yes]
)
case "$ans" in
 yes | no)
    case "$host" in
     *-*-openbsd*)
	ans=200
	;;
     *) ans=50
        ;;
    esac
    ;;
 [[1-9]][[0-9]]*)
    ;;
 *) AC_MSG_ERROR(["--with-stack-limit requires an integer argument."])
    ;;
esac
AC_MSG_RESULT([$ans])
AC_DEFINE_UNQUOTED([DFLT_RLIMIT_STACK], [$ans],
    [Default number of 4k pages for RLIMIT_STACK])

# HMS: only if we have RLIMIT_MEMLOCK
AC_MSG_CHECKING([for the default number of megabytes to MEMLOCK])
AC_ARG_WITH(
    [memlock],
    [AS_HELP_STRING(
	[--with-memlock],
	[? =32 (-1 on linux) megabytes]
    )],
    [ans=$withval],
    [ans=yes]
)
case "$ans" in
 yes | no)
    ans=$ntp_dflt_rlimit_memlock
    ;;
 [[1-9]][[0-9]]*) ;;
 *) AC_MSG_ERROR(["--with-memlock requires an integer argument."])
     ;;
esac
AC_MSG_RESULT([$ans])
AC_DEFINE_UNQUOTED([DFLT_RLIMIT_MEMLOCK], [$ans],
    [Default number of megabytes for RLIMIT_MEMLOCK])

])dnl

dnl ======================================================================
