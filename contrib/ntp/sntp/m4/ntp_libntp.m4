dnl ######################################################################
dnl Common m4sh code for libntp and clients
dnl
dnl Any configure tests which libntp or libisc code depends upon should
dnl be here or in another m4 macro used by the top-level and sntp
dnl configure.ac files, so that libntp can be moved into the sntp
dnl subpackage while retaining access to such test results.
dnl
AC_DEFUN([NTP_LIBNTP], [

AC_REQUIRE([NTP_CROSSCOMPILE])

# HMS: Save $LIBS and empty it.
# any LIBS we add here should go in to LDADD_LIBNTP
AC_SUBST([LDADD_LIBNTP])
__LIBS=$LIBS
LIBS=

dnl The contents of NTP_PROG_CC used to be here...

AC_PROG_INSTALL
# [Bug 2332] because we need to know if we are using GNU ld...
LT_PATH_LD

NTP_DIR_SEP
NTP_LINEEDITLIBS
NTP_LIB_M

AC_FUNC_FORK
AC_FUNC_ALLOCA
AC_FUNC_STRERROR_R

ac_busted_vpath_in_make=no
case "$build" in
 *-*-irix6.1*)	# 64 bit only
    # busted vpath?
    ;;
 *-*-irix6*)	# 6.2 (and later?)
    ac_busted_vpath_in_make=yes
    ;;
 *-*-solaris2.5.1)
    ac_busted_vpath_in_make=yes
    ;;
 *-*-unicosmp*)
    ac_busted_vpath_in_make=yes
    ;;
esac

case "$ac_busted_vpath_in_make$srcdir" in
 yes.|no*)
    ;;
 *) case "`${MAKE-make} -v -f /dev/null 2>/dev/null | grep 'GNU Make'`" in
     '')
	AC_MSG_ERROR([building outside of the main directory requires GNU make])
    esac
    ;;
esac

case "$host" in
 *-*-aix4*)
	# Bug 2516:
	# Was *-*-aix[[4-9]]*
	# XXX only verified thru AIX6.  But...
	# Ken Link says this is fine for AIX 5.3 and 7.1, and sees no reason
	# that this support would be in 5.3, removed in 6, and added back.
	#
	# (prr) aix 4.1 doesn't have clock_settime, but in aix 4.3 it's a stub
	# (returning ENOSYS).  I didn't check 4.2.  If, in the future,
	# IBM pulls its thumbs out long enough to implement clock_settime,
	# this conditional will need to change.  Maybe use AC_TRY_RUN
	# instead to try to set the time to itself and check errno.
    ;;
 *)
    HMS_SEARCH_LIBS([LDADD_LIBNTP], [clock_gettime], [rt])
    AC_CHECK_FUNCS([clock_getres clock_gettime clock_settime])
    ;;
esac

AC_CHECK_FUNCS([getclock stime timegm strlcpy strlcat])

# Bug 2713
LDADD_LIBUTIL=
AC_SUBST([LDADD_LIBUTIL])
HMS_SEARCH_LIBS([LDADD_LIBUTIL], [snprintb], [util],
		[AC_DEFINE([USE_SNPRINTB], 1, [OK to use snprintb()?])])

dnl  HP-UX 11.31 on HPPA has a net/if.h that can't be compiled with gcc4
dnl  due to an incomplete type (a union) mpinfou used in an array.  gcc3
dnl  compiles it without complaint.  The mpinfou union is defined later
dnl  in the resulting preprocessed source than the spu_info array in
dnl  /usr/include/machine/sys/getppdp.h:
dnl	extern union mpinfou spu_info[];
dnl  triggering the error.  Our strategy is on HP-UX only, test compile
dnl  net/if.h.  If that fails, try adding a duplicate definition of
dnl  mpinfou, and if that helps add it to confdefs.h (used for further
dnl  configure tests) and config.h.
#
AC_CHECK_HEADERS([errno.h sys/socket.h sys/types.h time.h])
AC_CHECK_HEADERS([net/if.h], [], [], [
    #ifdef HAVE_SYS_SOCKET_H
    # include <sys/socket.h>
    #endif
])
case "$host" in
 *-hp-hpux*)
    AC_CACHE_CHECK(
	[if net/if.h requires mpinfou predeclaration],
	[ntp_cv_predecl_mpinfou],
	[
	    np_cv_predecl_mpinfou=no
	    case "$ac_cv_header_net_if_h" in
	     no)
		AC_COMPILE_IFELSE(
		    [AC_LANG_PROGRAM(
			[[
			    typedef union mpinfou {
				    struct pdk_mpinfo *pdkptr;
				    struct mpinfo *pikptr;
			    } mpinfou_t;
			    #ifdef HAVE_SYS_SOCKET_H
			    # include <sys/socket.h>
			    #endif
			    #include <net/if.h>
			]],
			[[
			]]
		    )],
		    [
			ntp_cv_predecl_mpinfou=yes
			ac_cv_header_net_if_h=yes
		    ]
		)
	    esac
	]
    )
    case "$ntp_cv_predecl_mpinfou" in
     yes)
	cat >>confdefs.h <<_ACEOF
#ifndef MPINFOU_PREDECLARED
# define MPINFOU_PREDECLARED
typedef union mpinfou {
	struct pdk_mpinfo *pdkptr;
	struct mpinfo *pikptr;
} mpinfou_t;
#endif
_ACEOF
	AH_BOTTOM([
#ifndef MPINFOU_PREDECLARED
# define MPINFOU_PREDECLARED
typedef union mpinfou {
	struct pdk_mpinfo *pdkptr;
	struct mpinfo *pikptr;
} mpinfou_t;
#endif
])
    esac
esac

case "$host" in
 *-linux*)
    AC_CHECK_HEADERS([linux/if_addr.h], [], [], [
	#ifdef HAVE_SYS_SOCKET_H
	# include <sys/socket.h>
	#endif
    ])
    AC_DEFINE([NEED_EARLY_FORK], [1], [having to fork the DNS worker early when doing chroot?])
esac

AC_CHECK_HEADERS([arpa/nameser.h sys/param.h sys/time.h sys/timers.h])
# sys/sysctl.h depends on sys/param.h on OpenBSD - Bug 1576
AC_CHECK_HEADERS([sys/sysctl.h], [], [], [
    #if defined HAVE_SYS_PARAM_H
    # include <sys/param.h>
    #endif
])
AC_CHECK_HEADERS([netinet/in_system.h netinet/in_systm.h netinet/in.h])

AC_CHECK_HEADERS([resolv.h], [], [], [
    #ifdef HAVE_SYS_TYPES_H
    # include <sys/types.h>
    #endif
    #ifdef HAVE_NETINET_IN_H
    # include <netinet/in.h>
    #endif
    #ifdef HAVE_ARPA_NAMESER_H
    # include <arpa/nameser.h>
    #endif
])

AC_CHECK_HEADERS([net/if_var.h], [], [], [
    #if HAVE_SYS_TYPES_H
    # include <sys/types.h>
    #endif
    #ifdef HAVE_SYS_SOCKET_H
    # include <sys/socket.h>
    #endif
    #ifdef HAVE_NETINET_IN_H
    # include <netinet/in.h>
    #endif
    #ifdef HAVE_NET_IF_H
    # include <net/if.h>
    #endif
])

AC_CHECK_HEADERS([netinet/ip.h netinet/in_var.h], [], [], [
    #ifdef HAVE_SYS_TYPES_H
    # include <sys/types.h>
    #endif
    #ifdef HAVE_SYS_SOCKET_H
    # include <sys/socket.h>
    #endif
    #ifdef HAVE_NET_IF_H
    # include <net/if.h>
    #endif
    #ifdef HAVE_NETINET_IN_H
    # include <netinet/in.h>
    #endif
    #ifdef HAVE_NET_IF_VAR_H
    # include <net/if_var.h>
    #endif
    #ifdef HAVE_NETINET_IN_SYSTM_H
    # include <netinet/in_systm.h>
    #endif
])

# HMS: Do we need to check for -lsocket before or after these tests?
HMS_SEARCH_LIBS([LDADD_LIBNTP], [inet_pton], [nsl])
HMS_SEARCH_LIBS([LDADD_LIBNTP], [inet_ntop], [resolv], , , [-lnsl])

# [Bug 1628] On Solaris, we need -lxnet -lsocket.  Generalize this to
# avoid keying on the OS name:  If we find socket functions in
# libsocket, next try for them in libxnet without libsocket, if found,
# list both.  If we simply tested libxnet first, we would find the
# functions there and never add libsocket.  See also [Bug 660]
# http://bugs.ntp.org/show_bug.cgi?id=660#c9
saved_LIBS=$LIBS
HMS_SEARCH_LIBS([LDADD_LIBNTP], [setsockopt], [socket])
case "$ac_cv_search_setsockopt" in
 -lsocket)
    LIBS="$saved_LIBS"
    HMS_SEARCH_LIBS([LDADD_LIBNTP], [getsockopt], [xnet])
    # XXX Possible trouble here - reading the comments above and looking at the
    # code below I wonder if we'll add -lxnet when we don't need it.
    # Also, do we need to add -lxnet to LDADD_LIBNTP, or perhaps see if it's
    # there when it is not needed?
    case "$ac_cv_search_getsockopt" in
     -lxnet)
	LIBS="-lxnet -lsocket $saved_LIBS"
	;;
     *) LIBS="-lsocket $saved_LIBS"
	;;
    esac
    ;;
esac
AS_UNSET([saved_LIBS])

# Bug 2427 - look for recvmsg here.
AC_CHECK_FUNCS([recvmsg])

AC_C_INLINE

case "$ac_cv_c_inline" in
 '')
    ;;
 *)
    AC_DEFINE([HAVE_INLINE], [1], [inline keyword or macro available])
    AC_SUBST([HAVE_INLINE])
esac

AC_HEADER_TIME
AC_CHECK_SIZEOF([time_t])
AC_C_CHAR_UNSIGNED		dnl CROSS_COMPILE?
AC_CHECK_SIZEOF([signed char])
AC_CHECK_TYPES([s_char, long long])
AC_CHECK_SIZEOF([short])
AC_CHECK_SIZEOF([int])
AC_CHECK_SIZEOF([long])

case "$ac_cv_type_long_long" in
 no)
    ;;
 *)
    AC_CHECK_SIZEOF([long long])
    ;;
esac

case "$ac_cv_c_char_unsigned$ac_cv_sizeof_signed_char$ac_cv_type_s_char" in
 *yes)
    # We have a typedef for s_char.  Might as well believe it...
    ;;
 no0no)
    # We have signed chars, can't say 'signed char', no s_char typedef.
    AC_DEFINE([NEED_S_CHAR_TYPEDEF], [1],
	[Do we need an s_char typedef?])
    ;;
 no1no)
    # We have signed chars, can say 'signed char', no s_char typedef.
    AC_DEFINE([NEED_S_CHAR_TYPEDEF], [1],
	[Do we need an s_char typedef?])
    ;;
 yes0no)
    # We have unsigned chars, can't say 'signed char', no s_char typedef.
    AC_MSG_ERROR([No way to specify a signed character!])
    ;;
 yes1no)
    # We have unsigned chars, can say 'signed char', no s_char typedef.
    AC_DEFINE([NEED_S_CHAR_TYPEDEF], [1],
	[Do we need an s_char typedef?])
    ;;
esac

AC_TYPE_UID_T

m4_divert_text([HELP_ENABLE],
[AS_HELP_STRING([defaults:],
    [+ yes, - no, s system-specific])])

NTP_DEBUG

# check if we can compile with pthreads
AC_CHECK_HEADERS([semaphore.h])
AC_CHECK_FUNCS([socketpair])
AC_ARG_ENABLE(
    [thread-support],
    [AS_HELP_STRING([--enable-thread-support],
		    [s use threads (+ if available)])],
    [],
    [enable_thread_support=yes]
    )
have_pthreads=no
case "$enable_thread_support" in
 no) ;;
 *)
    ol_found_pthreads=no
    OL_THREAD_CHECK([ol_found_pthreads=yes])
    case "$ol_found_pthreads" in
     yes)
	saved_LIBS="$LIBS"
	LIBS="$LTHREAD_LIBS $LIBS"
	saved_CFLAGS="$CFLAGS"
	CFLAGS="$PTHREAD_CFLAGS $CFLAGS"
	AC_CHECK_FUNCS([sem_timedwait])
	LIBS="$saved_LIBS"
	AS_UNSET([saved_LIBS])
	CFLAGS="$saved_CFLAGS"
	AS_UNSET([saved_CFLAGS])
	case "$ac_cv_func_sem_timedwait" in
	 yes)
	    PTHREAD_LIBS="$LTHREAD_LIBS"
	    have_pthreads=yes
	esac
    esac
esac
AC_SUBST([PTHREAD_LIBS])
case "$have_pthreads" in
 yes)
    CFLAGS_NTP="$CFLAGS_NTP $PTHREAD_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LTHREAD_LIBS $LIBS"
    saved_CFLAGS="$CFLAGS"
    CFLAGS="$PTHREAD_CFLAGS $CFLAGS"
    AC_CHECK_SIZEOF(
	[pthread_t],
	,
	[
	    AC_INCLUDES_DEFAULT()
	    #include <pthread.h>
	]
    )
    LIBISC_PTHREADS_NOTHREADS=pthreads
    AC_DEFINE([ISC_PLATFORM_USETHREADS], [1],
	      [enable libisc thread support?])
    #
    # We'd like to use sigwait() too
    #
    AC_CHECK_FUNC(
	[sigwait],
	[have_sigwait=yes],
	[AC_CHECK_LIB(
	    [c],
	    [sigwait],
	    [have_sigwait=yes],
	    [AC_CHECK_LIB(
		[pthread],
		[sigwait],
		[have_sigwait=yes],
		[AC_CHECK_LIB(
		    [pthread],
		    [_Psigwait],
		    [have_sigwait=yes],
		    [have_sigwait=no]
		)]
	    )]
	)]
    )
    case "$host:$have_sigwait" in
     *-freebsd*:no)
	AC_CHECK_LIB(
	    [c_r],
	    [sigwait],
	    [have_sigwait=yes]
	)
    esac
    case "$have_sigwait" in
     yes)
	ac_cv_func_sigwait=yes
	AC_DEFINE([HAVE_SIGWAIT], [1], [sigwait() available?])
    esac

    AC_CHECK_FUNCS([pthread_attr_getstacksize])
    AC_CHECK_FUNCS([pthread_attr_setstacksize sysconf])

    case "$host" in
     *-freebsd5.[[012]]|*-freebsd5.[[012]].*)
	;;
     *-freebsd5.[[3456789]]|*-freebsd5.[[3456789]].*|*-freebsd6.*)
	AC_DEFINE([NEED_PTHREAD_SCOPE_SYSTEM], [1],
		  [use PTHREAD_SCOPE_SYSTEM?])
	;;
     *-bsdi3.*|*-bsdi4.0*)
	AC_DEFINE([NEED_PTHREAD_INIT], [1], [pthread_init() required?])
	;;
     *-linux*)
	AC_DEFINE([HAVE_LINUXTHREADS], [1], [using Linux pthread?])
	;;
     *-solaris*)
	AC_DEFINE([_POSIX_PTHREAD_SEMANTICS], [1])
	AC_CHECK_FUNCS([pthread_setconcurrency])
	case "$ac_cv_func_pthread_setconcurrency" in
	 yes)
	    AC_DEFINE([CALL_PTHREAD_SETCONCURRENCY], [1],
		      [why not HAVE_P_S?])
	esac
	;;
     *-sco-sysv*uw*|*-*-sysv*UnixWare*|*-*-sysv*OpenUNIX*)
	AC_DEFINE([HAVE_UNIXWARE_SIGWAIT], [1], [deviant sigwait?])
	;;
    esac
    hack_shutup_pthreadonceinit=no
    case "$host" in
     *-aix5.[[123]].*)
	hack_shutup_pthreadonceinit=yes
	;;
     *-solaris2.[[89]])
	hack_shutup_pthreadonceinit=yes
	;;
     *-solaris2.1[[0-9]])
	AC_CACHE_CHECK(
	    [if extra braces are needed for PTHREAD_ONCE_INIT],
	    [ntp_cv_braces_around_pthread_once_init],
	    [AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM(
		    [[
			#include <pthread.h>
		    ]],
		    [[
		        static pthread_once_t once_test =
						PTHREAD_ONCE_INIT;
		    ]]
		)],
		[ntp_cv_braces_around_pthread_once_init=no],
		[ntp_cv_braces_around_pthread_once_init=yes]
	    )]
	)
	case "$ntp_cv_braces_around_pthread_once_init" in
	 yes)
	    hack_shutup_pthreadonceinit=yes
	esac
	;;
    esac
    case "$hack_shutup_pthreadonceinit" in
     yes)
	AC_DEFINE([ISC_PLATFORM_BRACEPTHREADONCEINIT], [1],
		  [Enclose PTHREAD_ONCE_INIT in extra braces?])
    esac
    LIBS="$saved_LIBS"
    AS_UNSET([saved_LIBS])
    CFLAGS="$saved_CFLAGS"
    AS_UNSET([saved_CFLAGS])
    ;;
 *)
    LIBISC_PTHREADS_NOTHREADS=nothreads
    ;;
esac
AC_SUBST([LIBISC_PTHREADS_NOTHREADS])
AM_CONDITIONAL([PTHREADS], [test "$have_pthreads" != "no"])

AC_DEFUN([NTP_BEFORE_HW_FUNC_VSNPRINTF], [
    AC_BEFORE([$0], [HW_FUNC_VSNPRINTF])dnl
    AC_BEFORE([$0], [HW_FUNC_SNPRINTF])dnl
    AC_ARG_ENABLE(
	[c99-snprintf],
	[AS_HELP_STRING([--enable-c99-snprintf], [s force replacement])],
	[force_c99_snprintf=$enableval],
	[force_c99_snprintf=no]
	)
    case "$force_c99_snprintf" in
     yes)
	hw_force_rpl_snprintf=yes
	hw_force_rpl_vsnprintf=yes
    esac
    AH_VERBATIM(
	[snprinte],dnl	sorted in config.h just before #define snprintf
	[
	    #if !defined(_KERNEL) && !defined(PARSESTREAM)
	    /*
	     * stdio.h must be included after _GNU_SOURCE is defined
	     * but before #define snprintf rpl_snprintf
	     */
	    # include <stdio.h>	
	    #endif
	])
    AH_BOTTOM([
	#if !defined(_KERNEL) && !defined(PARSESTREAM)
	# if defined(HW_WANT_RPL_VSNPRINTF)
	#  if defined(__cplusplus)
	extern "C" {
	# endif
	# include <stdarg.h>
	int rpl_vsnprintf(char *, size_t, const char *, va_list);
	# if defined(__cplusplus)
	}
	#  endif
	# endif
	# if defined(HW_WANT_RPL_SNPRINTF)
	#  if defined(__cplusplus)
	extern "C" {
	#  endif
	int rpl_snprintf(char *, size_t, const char *, ...);
	#  if defined(__cplusplus)
	}
	#  endif
	# endif
	#endif	/* !defined(_KERNEL) && !defined(PARSESTREAM) */
	])
]) dnl end of AC_DEFUN of NTP_BEFORE_HW_FUNC_VSNPRINTF

AC_DEFUN([NTP_C99_SNPRINTF], [
    AC_REQUIRE([NTP_BEFORE_HW_FUNC_VSNPRINTF])dnl
    AC_REQUIRE([HW_FUNC_VSNPRINTF])dnl
    AC_REQUIRE([HW_FUNC_SNPRINTF])dnl
]) dnl end of DEFUN of NTP_C99_SNPRINTF

NTP_C99_SNPRINTF

dnl C99-snprintf does not handle %m
case "$hw_use_rpl_vsnprintf:$hw_cv_func_vsnprintf" in
 no:yes)
    AC_CACHE_CHECK(
	[if vsnprintf expands "%m" to strerror(errno)],
	[ntp_cv_vsnprintf_percent_m],
	[AC_RUN_IFELSE(
	    [AC_LANG_PROGRAM(
		[[
		    #include <stdarg.h>
		    #include <errno.h>
		    #include <stdio.h>
		    #include <string.h>

		    int call_vsnprintf(
			    char *	dst,
			    size_t	sz,
			    const char *fmt,
			    ...
			    );

		    int call_vsnprintf(
			    char *	dst,
			    size_t	sz,
			    const char *fmt,
			    ...
			    )
		    {
			    va_list	ap;
			    int		rc;

			    va_start(ap, fmt);
			    rc = vsnprintf(dst, sz, fmt, ap);
			    va_end(ap);

			    return rc;
		    }
		]],
		[[
		    char	sbuf[512];
		    char	pbuf[512];
		    int		slen;

		    strcpy(sbuf, strerror(ENOENT));
		    errno = ENOENT;
		    slen = call_vsnprintf(pbuf, sizeof(pbuf), "%m",
					  "wrong");
		    return strcmp(sbuf, pbuf);
		]]
	    )],
	    [ntp_cv_vsnprintf_percent_m=yes],
	    [ntp_cv_vsnprintf_percent_m=no],
	    [ntp_cv_vsnprintf_percent_m=no]
	)]
    )
    case "$ntp_cv_vsnprintf_percent_m" in
     yes)
	AC_DEFINE([VSNPRINTF_PERCENT_M], [1],
		  [vsnprintf expands "%m" to strerror(errno)])
    esac
esac

AC_CHECK_HEADERS([sys/clockctl.h])

AC_ARG_ENABLE(
    [clockctl],
    [AS_HELP_STRING(
	[--enable-clockctl],
	[s Use /dev/clockctl for non-root clock control]
    )],
    [ntp_use_dev_clockctl=$enableval],
    [ntp_use_dev_clockctl=$ac_cv_header_sys_clockctl_h]
)

AC_MSG_CHECKING([if we should use /dev/clockctl])
AC_MSG_RESULT([$ntp_use_dev_clockctl])


AC_CHECK_HEADERS([sys/capability.h sys/prctl.h])

AC_MSG_CHECKING([if we have linux capabilities (libcap)])

case "$ac_cv_header_sys_capability_h$ac_cv_header_sys_prctl_h" in
 yesyes)
    case "$host" in
     mips-sgi-irix*)
	ntp_have_linuxcaps=no
	;;
     *) ntp_have_linuxcaps=yes
	;;
    esac
    ;;
 *)
    ntp_have_linuxcaps=no
    ;;
esac

AC_ARG_ENABLE(
    [linuxcaps],
    [AS_HELP_STRING(
	[--enable-linuxcaps],
	[+ Use Linux capabilities for non-root clock control]
    )],
    [ntp_have_linuxcaps=$enableval]
)

AC_MSG_RESULT([$ntp_have_linuxcaps])

case "$ntp_have_linuxcaps" in
 yes)
    AC_DEFINE([HAVE_LINUX_CAPABILITIES], [1],
	[Do we have Linux capabilities?])
    LIBS="$LIBS -lcap"
    ;;
esac


AC_CHECK_HEADERS([priv.h])
case "$ac_cv_header_priv_h" in
 yes)
    case "$host" in 
     *-solaris*)
	AC_CHECK_FUNC(
    	    [setppriv],
    	    [ntp_have_solarisprivs=yes],
    	    [ntp_have_solarisprivs=no]
	)
	;;
    esac
esac

AC_ARG_ENABLE(
    [solarisprivs],
    [AS_HELP_STRING(
	[--enable-solarisprivs],
	[+ Use Solaris privileges for non-root clock control]
    )],
    [ntp_have_solarisprivs=$enableval]
)

AC_MSG_CHECKING([if we have solaris privileges])

case "$ntp_have_solarisprivs" in
 yes)
    AC_DEFINE([HAVE_SOLARIS_PRIVS], [1],
	[Are Solaris privileges available?])
    ;;
 '') ntp_have_solarisprivs="no"
    ;;
esac

AC_MSG_RESULT([$ntp_have_solarisprivs])

AC_CHECK_HEADERS([sys/mac.h])

AC_ARG_ENABLE(
    [trustedbsd_mac],
    [AS_HELP_STRING(
	[--enable-trustedbsd-mac],
	[s Use TrustedBSD MAC policy for non-root clock control]
    )],
    [ntp_use_trustedbsd_mac=$enableval]
)

AC_MSG_CHECKING([if we should use TrustedBSD MAC privileges])

case "$ntp_use_trustedbsd_mac$ac_cv_header_sys_mac_h" in
 yesyes)
    AC_DEFINE([HAVE_TRUSTEDBSD_MAC], [1],
	[Are TrustedBSD MAC policy privileges available?])
    ;;
 *) ntp_use_trustedbsd_mac="no";
    ;;
esac

AC_MSG_RESULT([$ntp_use_trustedbsd_mac])

case "$ntp_use_dev_clockctl$ntp_have_linuxcaps$ntp_have_solarisprivs$ntp_use_trustedbsd_mac" in
 *yes*)
    AC_DEFINE([HAVE_DROPROOT], [1],
	[Can we drop root privileges?])
esac

case "$host" in
 *-*-darwin*)
    AC_SEARCH_LIBS([res_9_init], [resolv])
    ;;
 *) AC_SEARCH_LIBS([res_init], [resolv])
    ;;
esac
AC_HEADER_RESOLV
#HMS: Why do we do this check so "early"?
AC_CHECK_FUNCS([res_init], , [AC_CHECK_FUNCS([__res_init])])

# We also need -lsocket, but we have tested for that already.
AC_CHECK_FUNC([inet_ntop], [], 
    [AC_DEFINE([ISC_PLATFORM_NEEDNTOP], [1], [ISC: provide inet_ntop()])])
AC_CHECK_FUNC([inet_pton], [],
    [AC_DEFINE([ISC_PLATFORM_NEEDPTON], [1], [ISC: provide inet_pton()])])

AC_CHECK_TYPES([uintptr_t, int32, u_int32])
 
AH_VERBATIM([TYPEDEF_UINTPTR_T],
[/* Provide a typedef for uintptr_t? */
#ifndef HAVE_UINTPTR_T
typedef unsigned int	uintptr_t;
#define HAVE_UINTPTR_T	1
#endif])

case "$ac_cv_type_int32::$ac_cv_header_resolv_h" in
 no::yes)
    AC_CACHE_CHECK(
	[for int32 with DNS headers included],
	[ntp_cv_type_int32_with_dns],
	[AC_COMPILE_IFELSE(
	    [AC_LANG_PROGRAM(
		[[
		    #ifdef HAVE_ARPA_NAMESER_H
		    # include <arpa/nameser.h>
		    #endif
		    #include <resolv.h>
		]],
		[[
		    size_t cb = sizeof(int32);
		]]
	    )],
	    [ntp_cv_type_int32_with_dns=yes],
	    [ntp_cv_type_int32_with_dns=no]
	)]
    )
    case "$ntp_cv_type_int32_with_dns" in
     yes)
	AC_DEFINE([HAVE_INT32_ONLY_WITH_DNS], [1],
	    [int32 type in DNS headers, not others.])
    esac
esac

case "$ac_cv_type_u_int32::$ac_cv_header_resolv_h" in
 no::yes)
    AC_CACHE_CHECK(
	[for u_int32 with DNS headers included],
	[ntp_cv_type_u_int32_with_dns],
	[AC_COMPILE_IFELSE(
	    [AC_LANG_PROGRAM(
		[[
		    #ifdef HAVE_ARPA_NAMESER_H
		    # include <arpa/nameser.h>
		    #endif
		    #include <resolv.h>
		]],
		[[
		    size_t cb = sizeof(u_int32);
		]]
	    )],
	    [ntp_cv_type_u_int32_with_dns=yes],
	    [ntp_cv_type_u_int32_with_dns=no]
	)]
    )
    case "$ntp_cv_type_u_int32_with_dns" in
     yes)
	AC_DEFINE([HAVE_U_INT32_ONLY_WITH_DNS], [1],
	    [u_int32 type in DNS headers, not others.])
    esac
esac

AC_CHECK_HEADERS(
    [sys/timepps.h],
    [],
    [],
    [
	#ifdef HAVE_SYS_TIME_H
	# include <sys/time.h>
	#endif
	#ifdef HAVE_ERRNO_H
	# include <errno.h>
	#endif
    ]
)

AC_CACHE_CHECK(
    [for struct timespec],
    [ntp_cv_struct_timespec],
    [AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM(
	    [[
		#include <sys/time.h>
		/* Under SunOS, timespec is in sys/timepps.h,
		   which needs errno.h and FRAC */
		#ifdef HAVE_ERRNO_H
		# include <errno.h>
		#endif
		#ifdef HAVE_SYS_TIMEPPS_H
		# define FRAC 4294967296
		# include <sys/timepps.h>
		#endif
	    ]],
	    [[
		struct timespec n;
	    ]]
	)],
	[ntp_cv_struct_timespec=yes],
	[ntp_cv_struct_timespec=no]
    )]
)
case "$ntp_cv_struct_timespec" in
 yes)
    AC_DEFINE([HAVE_STRUCT_TIMESPEC], [1], [struct timespec declared?])
esac

AC_CACHE_CHECK(
    [for struct ntptimeval],
    [ntp_cv_struct_ntptimeval],
    [AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM(
	    [[
		#include <sys/time.h>
		#include <sys/timex.h>
	    ]],
	    [[
		struct ntptimeval n;
	    ]]
	)],
	[ntp_cv_struct_ntptimeval=yes],
	[ntp_cv_struct_ntptimeval=no]
    )]
)
case "$ntp_cv_struct_ntptimeval" in
 yes)
    AC_DEFINE([HAVE_STRUCT_NTPTIMEVAL], [1],
	[Do we have struct ntptimeval?])
esac

AC_CHECK_HEADERS(
    [md5.h],
    [],
    [],
    [
	#ifdef HAVE_SYS_TYPES_H
	# include <sys/types.h>
	#endif
    ]
)

AC_SEARCH_LIBS([MD5Init], [md5 md])
AC_CHECK_FUNCS([MD5Init sysconf getdtablesize sigaction sigset sigvec])

# HMS: does this need to be a cached variable?
AC_ARG_ENABLE(
    [signalled-io],
    [AS_HELP_STRING([--enable-signalled-io], [s Use signalled IO if we can])],
    [use_signalled_io=$enableval],
    [use_signalled_io=yes]
    )

AC_CACHE_CHECK(
    [for SIGIO],
    [ntp_cv_hdr_def_sigio],
    [AC_PREPROC_IFELSE(
	[AC_LANG_SOURCE([
	    #include <signal.h>

	    #ifndef SIGIO
	    # error
	    #endif
	])],
	[ntp_cv_hdr_def_sigio=yes],
	[ntp_cv_hdr_def_sigio=no]
    )]
 )

dnl Override those system that have a losing SIGIO
AC_MSG_CHECKING([if we want to use SIGIO])
ans=no
case "$ntp_cv_hdr_def_sigio" in
 yes)
    ans=yes
    case "$host" in
     alpha*-dec-osf4*|alpha*-dec-osf5*)
	ans=no
	;;
     *-convex-*)
	ans=no
	;;
     *-dec-*)
	ans=no
	;;
     *-pc-cygwin*)
	ans=no
	;;
     *-sni-sysv*)
	ans=no
	;;
     *-stratus-vos)
	ans=no
	;;
     *-univel-sysv*)
	ans=no
	;;
     *-*-irix6*)
	ans=no
	;;
     *-*-freebsd*)
	ans=no
	;;
     *-*-*linux*)
	ans=no
	;;
     *-*-unicosmp*)
	ans=no
	;;
     *-*-kfreebsd*)
	ans=no
	;;
     m68k-*-mint*)
	ans=no
	;;
    esac
    case "$ans" in
     no)
	ans="Possible for $host but disabled because of reported problems"
	;;
    esac
    ;;
esac
case "$ans" in
 yes)
    case "$use_signalled_io" in
     yes)
	AC_DEFINE([HAVE_SIGNALED_IO], [1],
	    [Can we use SIGIO for tcp and udp IO?])
	;;
     *) ans="Allowed for $host but --disable-signalled-io was given"
	;;
    esac
esac
AC_MSG_RESULT([$ans])

AC_CACHE_CHECK(
    [for SIGPOLL],
    [ntp_cv_hdr_def_sigpoll],
    [AC_PREPROC_IFELSE(
	[AC_LANG_SOURCE([
	    #include <signal.h>
	    
	    #ifndef SIGPOLL
	    # error
	    #endif
	])],
	[ntp_cv_hdr_def_sigpoll=yes],
	[ntp_cv_hdr_def_sigpoll=no]
    )]
)

AC_MSG_CHECKING([if we can use SIGPOLL for UDP I/O])
ans=no
case "$ntp_cv_hdr_def_sigpoll" in
 yes)
    case "$host" in
     mips-sgi-irix*)
	ans=no
	;;
     vax-dec-bsd)
	ans=no
	;;
     *-pc-cygwin*)
	ans=no
	;;
     *-sni-sysv*)
	ans=no
	;;
     *-stratus-vos)
	ans=no
	;;
     *-*-aix[[4-9]]*)
	# XXX Only verified thru AIX6
	ans=no
	;;
     *-*-hpux*)
	ans=no
	;;
     *-*-*linux*)
	ans=no
	;;
     *-*-osf*)
	ans=no
	;;
     *-*-qnx*)
	ans=no
	;;
     *-*-sunos*)
	ans=no
	;;
     *-*-solaris*)
	ans=no
	;;
     *-*-ultrix*)
	ans=no
	;;
     *-*-unicosmp*)
	ans=no
	;;
     *-*-kfreebsd*)
	ans=no
	;;
     *) ans=yes
	;;
    esac
    ;;
esac
case "$ans" in
 yes)
    AC_DEFINE([USE_UDP_SIGPOLL], [1], [Can we use SIGPOLL for UDP?])
esac
AC_MSG_RESULT([$ans])

AC_MSG_CHECKING([if we can use SIGPOLL for TTY I/O])
ans=no
case "$ntp_cv_hdr_def_sigpoll" in
 yes)
    case "$host" in
     mips-sgi-irix*)
	ans=no
	;;
     vax-dec-bsd)
	ans=no
	;;
     *-pc-cygwin*)
	ans=no
	;;
     *-sni-sysv*)
	ans=no
	;;
     *-stratus-vos)
	ans=no
	;;
     *-*-aix[[4-9]]*)
	# XXX Only verified thru AIX6
	ans=no
	;;
     *-*-hpux*)
	ans=no
	;;
     *-*-*linux*)
	ans=no
	;;
     *-*-osf*)
	ans=no
	;;
     *-*-sunos*)
	ans=no
	;;
     *-*-ultrix*)
	ans=no
	;;
     *-*-qnx*)
	ans=no
	;;
     *-*-unicosmp*)
	ans=no
	;;
     *-*-kfreebsd*)
	ans=no
	;;
     *) ans=yes
	;;
    esac
    ;;
esac
case "$ans" in
 yes)
    AC_DEFINE([USE_TTY_SIGPOLL], [1], [Can we use SIGPOLL for tty IO?])
esac
AC_MSG_RESULT([$ans])

AC_CACHE_CHECK(
    [number of arguments to gettimeofday()],
    [ntp_cv_func_Xettimeofday_nargs],
    [AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM(
	    [[
		#include <sys/time.h>
	    ]],
	    [[
		gettimeofday(0, 0);
		settimeofday(0, 0);
	    ]]
	)],
	[ntp_cv_func_Xettimeofday_nargs=2],
	[ntp_cv_func_Xettimeofday_nargs=1]
    )]
)
case "$ntp_cv_func_Xettimeofday_nargs" in
 1)
    AC_DEFINE([SYSV_TIMEOFDAY], [1], [Does Xettimeofday take 1 arg?])
esac

AC_CHECK_FUNCS([settimeofday], ,[
    case "$host" in
     *-*-mpeix*) ac_cv_func_settimeofday=yes
    esac
])

AC_MSG_CHECKING([if we'll use clock_settime or settimeofday or stime])
ntp_warning='GRONK'
ans=none
case "$ac_cv_func_clock_settime$ac_cv_func_settimeofday$ac_cv_func_stime" in
 yes*)
    ntp_warning=''
    ans='clock_settime()'
    ;;
 noyes*)
    ntp_warning='But clock_settime() would be better (if we had it)'
    ans='settimeofday()'
    ;;
 nonoyes)
    ntp_warning='Which is the worst of the three'
    ans='stime()'
    ;;
 *) 
    case "$build" in
     $host) 
	ntp_warning='Which leaves us with nothing to use!'
    esac
esac
AC_MSG_RESULT([$ans])
case "$ntp_warning" in
 '')
    ;;
 *)
    AC_MSG_WARN([*** $ntp_warning ***])
    ;;
esac


dnl add to LDADD_LIBNTP set by ntp_compiler.m4
LDADD_LIBNTP="$LDADD_LIBNTP $LIBS"
LIBS=$__LIBS
AS_UNSET([__LIBS])

])dnl
dnl ======================================================================
