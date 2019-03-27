dnl OpenLDAP Autoconf thread check
dnl
dnl This work is part of OpenLDAP Software <http://www.openldap.org/>.
dnl
dnl Copyright 1998-2010 The OpenLDAP Foundation.
dnl All rights reserved.
dnl
dnl Redistribution and use in source and binary forms, with or without
dnl modification, are permitted only as authorized by the OpenLDAP
dnl Public License.
dnl
dnl A copy of this license is available in the file LICENSE-OPENLDAP in
dnl this directory of the distribution or, alternatively, at
dnl <http://www.OpenLDAP.org/license.html>.
dnl
dnl --------------------------------------------------------------------

dnl This file is a fragment of OpenLDAP's build/openldap.m4 and some
dnl fragments of OpenLDAP's configure.ac .

#   OL_THREAD_CHECK([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])

AC_DEFUN([OL_THREAD_CHECK], [
AC_REQUIRE([AC_CANONICAL_HOST])
AC_LANG_SAVE
AC_LANG([C])
OL_ARG_WITH(threads,[  --with-threads	  with threads],
	auto, [auto nt posix mach pth lwp yes no manual] )

case "$ol_with_threads$host" in
 auto*-*-solaris2.[[0-6]])
    dnl signals sometimes delivered to wrong thread with Solaris 2.6
    ol_with_threads=no
    ;;
esac

dnl AIX Thread requires we use cc_r or xlc_r.
dnl But only do this IF AIX and CC is not set
dnl and threads are auto|yes|posix.
dnl
dnl If we find cc_r|xlc_r, force pthreads and assume
dnl		pthread_create is in $LIBS (ie: don't bring in
dnl		any additional thread libraries)
dnl If we do not find cc_r|xlc_r, disable threads

ol_aix_threads=no
case "$host" in
*-*-aix*) dnl all AIX is not a good idea.
	if test -z "$CC" ; then
		case "$ol_with_threads" in
		auto | yes |  posix) ol_aix_threads=yes ;;
		esac
	fi
;;
esac

if test $ol_aix_threads = yes ; then
	if test -z "${CC}" ; then
		AC_CHECK_PROGS(CC,cc_r xlc_r cc)

		if test "$CC" = cc ; then
			dnl no CC! don't allow --with-threads
			if test $ol_with_threads != auto ; then
				AC_MSG_ERROR([--with-threads requires cc_r (or other suitable compiler) on AIX])
			else
				AC_MSG_WARN([disabling threads, no cc_r on AIX])
			fi
			ol_with_threads=no
  		fi
	fi

	case ${CC} in cc_r | xlc_r)
		ol_with_threads=posix
		ol_cv_pthread_create=yes
		;;
	esac
fi

dnl ----------------------------------------------------------------
dnl Threads?
ol_link_threads=no
dnl ol_with_yielding_select=${ol_with_yielding_select:-auto}
OL_ARG_WITH(yielding_select,[  --with-yielding-select  with yielding select],
	auto, [auto yes no manual] )

case $ol_with_threads in auto | yes | nt)

	OL_NT_THREADS

	if test "$ol_cv_nt_threads" = yes ; then
		ol_link_threads=nt
		ol_with_threads=found
		ol_with_yielding_select=yes

		AC_DEFINE([HAVE_NT_SERVICE_MANAGER], [1], [if you have NT Service Manager])
		AC_DEFINE([HAVE_NT_EVENT_LOG], [1], [if you have NT Event Log])
	fi

	if test $ol_with_threads = nt ; then
		AC_MSG_ERROR([could not locate NT Threads])
	fi
	;;
esac

case $ol_with_threads in auto | yes | posix)

	AC_CHECK_HEADERS(pthread.h)

	if test $ac_cv_header_pthread_h = yes ; then
		OL_POSIX_THREAD_VERSION

		if test $ol_cv_pthread_version != 0 ; then
			AC_DEFINE_UNQUOTED([HAVE_PTHREADS], [$ol_cv_pthread_version],
				[define to pthreads API spec revision])
		else
			AC_MSG_ERROR([unknown pthread version])
		fi

		# consider threads found
		ol_with_threads=found

		OL_HEADER_LINUX_THREADS
		OL_HEADER_GNU_PTH_PTHREAD_H

		if test $ol_cv_header_gnu_pth_pthread_h = no ; then
			AC_CHECK_HEADERS(sched.h)
		fi

		dnl Now the hard part, how to link?
		dnl
		dnl currently supported checks:
		dnl
		dnl Check for no flags 
		dnl 	pthread_create() in $LIBS
		dnl
		dnl Check special pthread (final) flags
		dnl 	[skipped] pthread_create() with -mt (Solaris) [disabled]
		dnl 	pthread_create() with -kthread (FreeBSD)
		dnl 	pthread_create() with -pthread (FreeBSD/Digital Unix)
		dnl 	pthread_create() with -pthreads (?)
		dnl 	pthread_create() with -mthreads (AIX)
		dnl 	pthread_create() with -thread (?)
		dnl
		dnl Check pthread (final) libraries
		dnl 	pthread_mutex_unlock() in -lpthread -lmach -lexc -lc_r (OSF/1)
		dnl 	pthread_mutex_lock() in -lpthread -lmach -lexc (OSF/1)
		dnl 	[skipped] pthread_mutex_trylock() in -lpthread -lexc (OSF/1)
		dnl 	pthread_join() -Wl,-woff,85 -lpthread (IRIX)
		dnl 	pthread_create() in -lpthread (many)
		dnl 	pthread_create() in -lc_r (FreeBSD)
		dnl
		dnl Check pthread (draft4) flags (depreciated)
		dnl 	pthread_create() with -threads (OSF/1)
		dnl
		dnl Check pthread (draft4) libraries (depreciated)
		dnl 	pthread_mutex_unlock() in -lpthreads -lmach -lexc -lc_r (OSF/1)
		dnl 	pthread_mutex_lock() in -lpthreads -lmach -lexc (OSF/1)
		dnl 	pthread_mutex_trylock() in -lpthreads -lexc (OSF/1)
		dnl 	pthread_create() in -lpthreads (many)
		dnl

		dnl pthread_create in $LIBS
		AC_CACHE_CHECK([for pthread_create in default libraries],
			ol_cv_pthread_create,[
			AC_RUN_IFELSE([OL_PTHREAD_TEST_PROGRAM],
				[ol_cv_pthread_create=yes],
				[ol_cv_pthread_create=no],
				[AC_TRY_LINK(OL_PTHREAD_TEST_INCLUDES,OL_PTHREAD_TEST_FUNCTION,
					[ol_cv_pthread_create=yes],
					[ol_cv_pthread_create=no])])])

		if test $ol_cv_pthread_create != no ; then
			ol_link_threads=posix
			ol_link_pthreads=""
		fi
		
dnl		OL_PTHREAD_TRY([-mt],		[ol_cv_pthread_mt])
		OL_PTHREAD_TRY([-kthread],	[ol_cv_pthread_kthread])
		OL_PTHREAD_TRY([-pthread],	[ol_cv_pthread_pthread])
		OL_PTHREAD_TRY([-pthreads],	[ol_cv_pthread_pthreads])
		OL_PTHREAD_TRY([-mthreads],	[ol_cv_pthread_mthreads])
		OL_PTHREAD_TRY([-thread],	[ol_cv_pthread_thread])

		OL_PTHREAD_TRY([-lpthread -lmach -lexc -lc_r],
			[ol_cv_pthread_lpthread_lmach_lexc_lc_r])
		OL_PTHREAD_TRY([-lpthread -lmach -lexc],
			[ol_cv_pthread_lpthread_lmach_lexc])
dnl		OL_PTHREAD_TRY([-lpthread -lexc],
dnl			[ol_cv_pthread_lpthread_lexc])

		OL_PTHREAD_TRY([-lpthread -Wl,-woff,85],
			[ol_cv_pthread_lib_lpthread_woff])

		OL_PTHREAD_TRY([-lpthread],	[ol_cv_pthread_lpthread])
		OL_PTHREAD_TRY([-lc_r],		[ol_cv_pthread_lc_r])

		OL_PTHREAD_TRY([-threads],	[ol_cv_pthread_threads])

		OL_PTHREAD_TRY([-lpthreads -lmach -lexc -lc_r],
			[ol_cv_pthread_lpthreads_lmach_lexc_lc_r])
		OL_PTHREAD_TRY([-lpthreads -lmach -lexc],
			[ol_cv_pthread_lpthreads_lmach_lexc])
		OL_PTHREAD_TRY([-lpthreads -lexc],
			[ol_cv_pthread_lpthreads_lexc])

		OL_PTHREAD_TRY([-lpthreads],[ol_cv_pthread_lib_lpthreads])

AC_MSG_NOTICE([ol_link_threads: <$ol_link_threads> ol_link_pthreads <$ol_link_pthreads>])

		if test $ol_link_threads != no ; then
			LTHREAD_LIBS="$LTHREAD_LIBS $ol_link_pthreads"

			dnl save flags
			save_CPPFLAGS="$CPPFLAGS"
			save_LIBS="$LIBS"
			LIBS="$LTHREAD_LIBS $LIBS"

			dnl All POSIX Thread (final) implementations should have
			dnl sched_yield instead of pthread yield.
			dnl check for both, and thr_yield for Solaris
			AC_CHECK_FUNCS(sched_yield pthread_yield thr_yield)

			if test $ac_cv_func_sched_yield = no &&
			   test $ac_cv_func_pthread_yield = no &&
			   test $ac_cv_func_thr_yield = no ; then
				dnl Digital UNIX has sched_yield() in -lrt
				AC_CHECK_LIB(rt, sched_yield,
					[LTHREAD_LIBS="$LTHREAD_LIBS -lrt"
					AC_DEFINE([HAVE_SCHED_YIELD], [1],
						[Define if you have the sched_yield function.])
					ac_cv_func_sched_yield=yes],
					[ac_cv_func_sched_yield=no])
			fi
			if test $ac_cv_func_sched_yield = no &&
			   test $ac_cv_func_pthread_yield = no &&
			   test "$ac_cv_func_thr_yield" = no ; then
				AC_MSG_WARN([could not locate sched_yield() or pthread_yield()])
			fi

			dnl Check functions for compatibility
			AC_CHECK_FUNCS(pthread_kill)

			dnl Check for pthread_rwlock_destroy with <pthread.h>
			dnl as pthread_rwlock_t may not be defined.
			AC_CACHE_CHECK([for pthread_rwlock_destroy with <pthread.h>],
				[ol_cv_func_pthread_rwlock_destroy], [
				dnl save the flags
				AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <pthread.h>
pthread_rwlock_t rwlock;
]], [[pthread_rwlock_destroy(&rwlock);]])],[ol_cv_func_pthread_rwlock_destroy=yes],[ol_cv_func_pthread_rwlock_destroy=no])
			])
			if test $ol_cv_func_pthread_rwlock_destroy = yes ; then
				AC_DEFINE([HAVE_PTHREAD_RWLOCK_DESTROY], [1],
					[define if you have pthread_rwlock_destroy function])
			fi

			dnl Check for pthread_detach with <pthread.h> inclusion
			dnl as it's symbol may have been mangled.
			AC_CACHE_CHECK([for pthread_detach with <pthread.h>],
				[ol_cv_func_pthread_detach], [
				dnl save the flags
				AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <pthread.h>
#ifndef NULL
#define NULL (void*)0
#endif
]], [[pthread_detach(NULL);]])],[ol_cv_func_pthread_detach=yes],[ol_cv_func_pthread_detach=no])
			])

			if test $ol_cv_func_pthread_detach = no ; then
				AC_MSG_ERROR([could not locate pthread_detach()])
			fi

			AC_DEFINE([HAVE_PTHREAD_DETACH], [1],
				[define if you have pthread_detach function])

			dnl Check for setconcurreny functions
			AC_CHECK_FUNCS(	\
				pthread_setconcurrency \
				pthread_getconcurrency \
				thr_setconcurrency \
				thr_getconcurrency \
			)

			OL_SYS_LINUX_THREADS
			OL_LINUX_THREADS

			if test $ol_cv_linux_threads = error; then
				AC_MSG_ERROR([LinuxThreads header/library mismatch]);
			fi

			AC_CACHE_CHECK([if pthread_create() works],
				ol_cv_pthread_create_works,[
			AC_RUN_IFELSE([OL_PTHREAD_TEST_PROGRAM],
				[ol_cv_pthread_create_works=yes],
				[ol_cv_pthread_create_works=no],
				[dnl assume yes
				ol_cv_pthread_create_works=yes])])

			if test $ol_cv_pthread_create_works = no ; then
				AC_MSG_ERROR([pthread_create is not usable, check environment settings])
			fi

			ol_replace_broken_yield=no
dnl			case "$host" in
dnl			*-*-linux*) 
dnl				AC_CHECK_FUNCS(nanosleep)
dnl				ol_replace_broken_yield=yes
dnl			;;
dnl			esac

			if test $ol_replace_broken_yield = yes ; then
				AC_DEFINE([REPLACE_BROKEN_YIELD], [1],
					[define if sched_yield yields the entire process])
			fi

			dnl Check if select causes an yield
			if test x$ol_with_yielding_select = xauto ; then
				AC_CACHE_CHECK([if select yields when using pthreads],
					ol_cv_pthread_select_yields,[
				AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#ifndef NULL
#define NULL (void*) 0
#endif

static int fildes[2];

static void *task(p)
	void *p;
{
	int i;
	struct timeval tv;

	fd_set rfds;

	tv.tv_sec=10;
	tv.tv_usec=0;

	FD_ZERO(&rfds);
	FD_SET(fildes[0], &rfds);

	/* we're not interested in any fds */
	i = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);

	if(i < 0) {
		perror("select");
		exit(10);
	}

	exit(0); /* if we exit here, the select blocked the whole process */
}

int main(argc, argv)
	int argc;
	char **argv;
{
	pthread_t t;

	/* create a pipe to select */
	if(pipe(&fildes[0])) {
		perror("select");
		exit(1);
	}

#ifdef HAVE_PTHREAD_SETCONCURRENCY
	(void) pthread_setconcurrency(2);
#else
#ifdef HAVE_THR_SETCONCURRENCY
	/* Set Solaris LWP concurrency to 2 */
	thr_setconcurrency(2);
#endif
#endif

#if HAVE_PTHREADS < 6
	pthread_create(&t, pthread_attr_default, task, NULL);
#else
	pthread_create(&t, NULL, task, NULL);
#endif

	/* make sure task runs first */
#ifdef HAVE_THR_YIELD
	thr_yield();
#elif defined( HAVE_SCHED_YIELD )
	sched_yield();
#elif defined( HAVE_PTHREAD_YIELD )
	pthread_yield();
#endif

	exit(2);
}]])],[ol_cv_pthread_select_yields=no],[ol_cv_pthread_select_yields=yes],[ol_cv_pthread_select_yields=cross])])

				if test $ol_cv_pthread_select_yields = cross ; then
					AC_MSG_ERROR([crossing compiling: use --with-yielding-select=yes|no|manual])
				fi

				if test $ol_cv_pthread_select_yields = yes ; then
					ol_with_yielding_select=yes
				fi
			fi

			dnl restore flags
			CPPFLAGS="$save_CPPFLAGS"
			LIBS="$save_LIBS"
		else
			AC_MSG_ERROR([could not locate usable POSIX Threads])
		fi
	fi

	if test $ol_with_threads = posix ; then
		AC_MSG_ERROR([could not locate POSIX Threads])
	fi
	;;
esac

case $ol_with_threads in auto | yes | mach)

	dnl check for Mach CThreads
	AC_CHECK_HEADERS(mach/cthreads.h cthreads.h)
	if test $ac_cv_header_mach_cthreads_h = yes ; then
		ol_with_threads=found

		dnl check for cthreads support in current $LIBS
		AC_CHECK_FUNC(cthread_fork,[ol_link_threads=yes])

		if test $ol_link_threads = no ; then
			dnl try -all_load
			dnl this test needs work
			AC_CACHE_CHECK([for cthread_fork with -all_load],
				[ol_cv_cthread_all_load], [
				dnl save the flags
				save_LIBS="$LIBS"
				LIBS="-all_load $LIBS"
				AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <mach/cthreads.h>]], [[
					cthread_fork((void *)0, (void *)0);
					]])],[ol_cv_cthread_all_load=yes],[ol_cv_cthread_all_load=no])
				dnl restore the LIBS
				LIBS="$save_LIBS"
			])

			if test $ol_cv_cthread_all_load = yes ; then
				LTHREAD_LIBS="$LTHREAD_LIBS -all_load"
				ol_link_threads=mach
				ol_with_threads=found
			fi
		fi

	elif test $ac_cv_header_cthreads_h = yes ; then
		dnl Hurd variant of Mach Cthreads
		dnl uses <cthreads.h> and -lthreads

		ol_with_threads=found
 
		dnl save the flags
		save_LIBS="$LIBS"
		LIBS="$LIBS -lthreads"
		AC_CHECK_FUNC(cthread_fork,[ol_link_threads=yes])
		LIBS="$save_LIBS"

		if test $ol_link_threads = yes ; then
			LTHREAD_LIBS="-lthreads"
			ol_link_threads=mach
			ol_with_threads=found
		else
			AC_MSG_ERROR([could not link with Mach CThreads])
		fi

	elif test $ol_with_threads = mach ; then
		AC_MSG_ERROR([could not locate Mach CThreads])
	fi

	if test $ol_link_threads = mach ; then
		AC_DEFINE([HAVE_MACH_CTHREADS], [1],
			[define if you have Mach Cthreads])
	elif test $ol_with_threads = found ; then
		AC_MSG_ERROR([could not link with Mach CThreads])
	fi
	;;
esac

case $ol_with_threads in auto | yes | pth)

	AC_CHECK_HEADERS(pth.h)

	if test $ac_cv_header_pth_h = yes ; then
		AC_CHECK_LIB(pth, pth_version, [have_pth=yes], [have_pth=no])

		if test $have_pth = yes ; then
			AC_DEFINE([HAVE_GNU_PTH], [1], [if you have GNU Pth])
			LTHREAD_LIBS="$LTHREAD_LIBS -lpth"
			ol_link_threads=pth
			ol_with_threads=found

			if test x$ol_with_yielding_select = xauto ; then
				ol_with_yielding_select=yes
			fi
		fi
	fi
	;;
esac

case $ol_with_threads in auto | yes | lwp)

	dnl check for SunOS5 LWP
	AC_CHECK_HEADERS(thread.h synch.h)
	if test $ac_cv_header_thread_h = yes &&
	   test $ac_cv_header_synch_h = yes ; then
		AC_CHECK_LIB(thread, thr_create, [have_thr=yes], [have_thr=no])

		if test $have_thr = yes ; then
			AC_DEFINE([HAVE_THR], [1],
				[if you have Solaris LWP (thr) package])
			LTHREAD_LIBS="$LTHREAD_LIBS -lthread"
			ol_link_threads=thr

			if test x$ol_with_yielding_select = xauto ; then
				ol_with_yielding_select=yes
			fi

			dnl Check for setconcurrency functions
			AC_CHECK_FUNCS(	\
				thr_setconcurrency \
				thr_getconcurrency \
			)
		fi
	fi

	dnl check for SunOS4 LWP
	AC_CHECK_HEADERS(lwp/lwp.h)
	if test $ac_cv_header_lwp_lwp_h = yes ; then
		AC_CHECK_LIB(lwp, lwp_create, [have_lwp=yes], [have_lwp=no])

		if test $have_lwp = yes ; then
			AC_DEFINE([HAVE_LWP], [1],
				[if you have SunOS LWP package])
			LTHREAD_LIBS="$LTHREAD_LIBS -llwp"
			ol_link_threads=lwp

			if test x$ol_with_yielding_select = xauto ; then
				ol_with_yielding_select=no
			fi
		fi
	fi
	;;
esac

if test $ol_with_yielding_select = yes ; then
	AC_DEFINE([HAVE_YIELDING_SELECT], [1],
		[define if select implicitly yields])
fi

if test $ol_with_threads = manual ; then
	dnl User thinks he can manually configure threads.
	ol_link_threads=yes

	AC_MSG_WARN([thread defines and link options must be set manually])

	AC_CHECK_HEADERS(pthread.h sched.h)
	AC_CHECK_FUNCS(sched_yield pthread_yield)
	OL_HEADER_LINUX_THREADS

	AC_CHECK_HEADERS(mach/cthreads.h)
	AC_CHECK_HEADERS(lwp/lwp.h)
	AC_CHECK_HEADERS(thread.h synch.h)
fi

if test $ol_link_threads != no && test $ol_link_threads != nt ; then
	dnl needed to get reentrant/threadsafe versions
	dnl
	AC_DEFINE([REENTRANT], [1], [enable thread safety])
	AC_DEFINE([_REENTRANT], [1], [enable thread safety])
	AC_DEFINE([THREAD_SAFE], [1], [enable thread safety])
	AC_DEFINE([_THREAD_SAFE], [1], [enable thread safety])
	AC_DEFINE([THREADSAFE], [1], [enable thread safety])
	AC_DEFINE([_THREADSAFE], [1], [enable thread safety])
	AC_DEFINE([_SGI_MP_SOURCE], [1], [enable thread safety])

	dnl The errno declaration may dependent upon _REENTRANT.
	dnl If it does, we must link with thread support.
	AC_CACHE_CHECK([for thread specific errno],
		[ol_cv_errno_thread_specific], [
		AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <errno.h>]], [[errno = 0;]])],[ol_cv_errno_thread_specific=yes],[ol_cv_errno_thread_specific=no])
	])

	dnl The h_errno declaration may dependent upon _REENTRANT.
	dnl If it does, we must link with thread support.
	AC_CACHE_CHECK([for thread specific h_errno],
		[ol_cv_h_errno_thread_specific], [
		AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <netdb.h>]], [[h_errno = 0;]])],[ol_cv_h_errno_thread_specific=yes],[ol_cv_h_errno_thread_specific=no])
	])

	if test $ol_cv_errno_thread_specific != yes ||
	   test $ol_cv_h_errno_thread_specific != yes ; then
		LIBS="$LTHREAD_LIBS $LIBS"
		LTHREAD_LIBS=""
	fi

dnl When in thread environment, use 
dnl		#if defined( HAVE_REENTRANT_FUNCTIONS ) || defined( HAVE_FUNC_R )
dnl			func_r(...);
dnl		#else
dnl		#	if defined( HAVE_THREADS ) 
dnl				/* lock */
dnl		#	endif
dnl				func(...);
dnl		#	if defined( HAVE_THREADS ) 
dnl				/* unlock */
dnl		#	endif
dnl		#endif
dnl
dnl HAVE_REENTRANT_FUNCTIONS is derived from:
dnl		_POSIX_REENTRANT_FUNCTIONS
dnl		_POSIX_THREAD_SAFE_FUNCTIONS
dnl		_POSIX_THREADSAFE_FUNCTIONS
dnl
dnl		and is currently defined in <ldap_pvt_thread.h>
dnl
dnl HAVE_THREADS is defined by <ldap_pvt_thread.h> iff -UNO_THREADS
dnl 
dnl libldap/*.c should only include <ldap_pvt_thread.h> iff
dnl LDAP_R_COMPILE is defined.  ie:
dnl		#ifdef LDAP_R_COMPILE
dnl		#	include <ldap_pvt_thread.h>
dnl		#endif
dnl
dnl LDAP_R_COMPILE is defined by libldap_r/Makefile.in
dnl specifically for compiling the threadsafe version of
dnl	the ldap library (-lldap_r).
dnl		
dnl	dnl check for reentrant/threadsafe functions
dnl	dnl
dnl	dnl note: these should only be used when linking
dnl	dnl		with $LTHREAD_LIBS
dnl	dnl
dnl	save_CPPFLAGS="$CPPFLAGS"
dnl	save_LIBS="$LIBS"
dnl	LIBS="$LTHREAD_LIBS $LIBS"
dnl	AC_CHECK_FUNCS(	\
dnl		gmtime_r \
dnl		gethostbyaddr_r gethostbyname_r \
dnl		feof_unlocked unlocked_feof \
dnl		putc_unlocked unlocked_putc \
dnl		flockfile ftrylockfile \
dnl	)
dnl	CPPFLAGS="$save_CPPFLAGS"
dnl	LIBS="$save_LIBS"
fi  

if test $ol_link_threads = no ; then
	if test $ol_with_threads = yes ; then
		AC_MSG_ERROR([no suitable thread support])
	fi

	if test $ol_with_threads = auto ; then
		AC_MSG_WARN([no suitable thread support, disabling threads])
		ol_with_threads=no
	fi

	AC_DEFINE([NO_THREADS], [1],
		[define if you have (or want) no threads])
	LTHREAD_LIBS=""
	BUILD_THREAD=no
else
	BUILD_THREAD=yes
fi

if test $ol_link_threads != no ; then
	AC_DEFINE([LDAP_API_FEATURE_X_OPENLDAP_THREAD_SAFE], [1],
		[define to 1 if library is thread safe])
fi

# Finally, execute ACTION-IF-FOUND/ACTION-IF-NOT-FOUND:
case "$ol_with_threads" in
 no)
    ol_pthread_ok=no
    $2
    ;;
 *)
    ifelse([$1],,AC_DEFINE(HAVE_PTHREAD,1,[Define if you have POSIX threads libraries and header files.]),[$1])
    ;;
esac

AC_LANG_RESTORE

AC_SUBST(BUILD_THREAD)
AC_SUBST(LTHREAD_LIBS)

])
