# This was retrieved from
#    http://svn.0pointer.de/viewvc/trunk/common/acx_pthread.m4?revision=1277&root=avahi
# See also (perhaps for new versions?)
#    http://svn.0pointer.de/viewvc/trunk/common/acx_pthread.m4?root=avahi
#
# We've rewritten the inconsistency check code (from avahi), to work
# more broadly.  In particular, it no longer assumes ld accepts -zdefs.
# This caused a restructing of the code, but the functionality has only
# changed a little.

dnl @synopsis ACX_PTHREAD([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
dnl
dnl @summary figure out how to build C programs using POSIX threads
dnl
dnl This macro figures out how to build C programs using POSIX threads.
dnl It sets the PTHREAD_LIBS output variable to the threads library and
dnl linker flags, and the PTHREAD_CFLAGS output variable to any special
dnl C compiler flags that are needed. (The user can also force certain
dnl compiler flags/libs to be tested by setting these environment
dnl variables.)
dnl
dnl Also sets PTHREAD_CC to any special C compiler that is needed for
dnl multi-threaded programs (defaults to the value of CC otherwise).
dnl (This is necessary on AIX to use the special cc_r compiler alias.)
dnl
dnl NOTE: You are assumed to not only compile your program with these
dnl flags, but also link it with them as well. e.g. you should link
dnl with $PTHREAD_CC $CFLAGS $PTHREAD_CFLAGS $LDFLAGS ... $PTHREAD_LIBS
dnl $LIBS
dnl
dnl If you are only building threads programs, you may wish to use
dnl these variables in your default LIBS, CFLAGS, and CC:
dnl
dnl        LIBS="$PTHREAD_LIBS $LIBS"
dnl        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
dnl        CC="$PTHREAD_CC"
dnl
dnl In addition, if the PTHREAD_CREATE_JOINABLE thread-attribute
dnl constant has a nonstandard name, defines PTHREAD_CREATE_JOINABLE to
dnl that name (e.g. PTHREAD_CREATE_UNDETACHED on AIX).
dnl
dnl ACTION-IF-FOUND is a list of shell commands to run if a threads
dnl library is found, and ACTION-IF-NOT-FOUND is a list of commands to
dnl run it if it is not found. If ACTION-IF-FOUND is not specified, the
dnl default action will define HAVE_PTHREAD.
dnl
dnl Please let the authors know if this macro fails on any platform, or
dnl if you have any other suggestions or comments. This macro was based
dnl on work by SGJ on autoconf scripts for FFTW (www.fftw.org) (with
dnl help from M. Frigo), as well as ac_pthread and hb_pthread macros
dnl posted by Alejandro Forero Cuervo to the autoconf macro repository.
dnl We are also grateful for the helpful feedback of numerous users.
dnl
dnl @category InstalledPackages
dnl @author Steven G. Johnson <stevenj@alum.mit.edu>
dnl @version 2006-05-29
dnl @license GPLWithACException
dnl 
dnl Checks for GCC shared/pthread inconsistency based on work by
dnl Marcin Owsiany <marcin@owsiany.pl>


AC_DEFUN([ACX_PTHREAD], [
AC_REQUIRE([AC_CANONICAL_HOST])
AC_LANG_SAVE
AC_LANG_C
acx_pthread_ok=no

# We used to check for pthread.h first, but this fails if pthread.h
# requires special compiler flags (e.g. on True64 or Sequent).
# It gets checked for in the link test anyway.

# First of all, check if the user has set any of the PTHREAD_LIBS,
# etcetera environment variables, and if threads linking works using
# them:
if test x"$PTHREAD_LIBS$PTHREAD_CFLAGS" != x; then
        save_CFLAGS="$CFLAGS"
        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
        save_LIBS="$LIBS"
        LIBS="$PTHREAD_LIBS $LIBS"
        AC_MSG_CHECKING([for pthread_join in LIBS=$PTHREAD_LIBS with CFLAGS=$PTHREAD_CFLAGS])
        AC_TRY_LINK_FUNC(pthread_join, acx_pthread_ok=yes)
        AC_MSG_RESULT($acx_pthread_ok)
        if test x"$acx_pthread_ok" = xno; then
                PTHREAD_LIBS=""
                PTHREAD_CFLAGS=""
        fi
        LIBS="$save_LIBS"
        CFLAGS="$save_CFLAGS"
fi

# We must check for the threads library under a number of different
# names; the ordering is very important because some systems
# (e.g. DEC) have both -lpthread and -lpthreads, where one of the
# libraries is broken (non-POSIX).

# Create a list of thread flags to try.  Items starting with a "-" are
# C compiler flags, and other items are library names, except for "none"
# which indicates that we try without any flags at all, and "pthread-config"
# which is a program returning the flags for the Pth emulation library.

acx_pthread_flags="pthreads none -Kthread -kthread lthread -pthread -pthreads -mthreads pthread --thread-safe -mt pthread-config"

# The ordering *is* (sometimes) important.  Some notes on the
# individual items follow:

# pthreads: AIX (must check this before -lpthread)
# none: in case threads are in libc; should be tried before -Kthread and
#       other compiler flags to prevent continual compiler warnings
# -Kthread: Sequent (threads in libc, but -Kthread needed for pthread.h)
# -kthread: FreeBSD kernel threads (preferred to -pthread since SMP-able)
# lthread: LinuxThreads port on FreeBSD (also preferred to -pthread)
# -pthread: Linux/gcc (kernel threads), BSD/gcc (userland threads)
# -pthreads: Solaris/gcc
# -mthreads: Mingw32/gcc, Lynx/gcc
# -mt: Sun Workshop C (may only link SunOS threads [-lthread], but it
#      doesn't hurt to check since this sometimes defines pthreads too;
#      also defines -D_REENTRANT)
#      ... -mt is also the pthreads flag for HP/aCC
# pthread: Linux, etcetera
# --thread-safe: KAI C++
# pthread-config: use pthread-config program (for GNU Pth library)

case "${host_cpu}-${host_os}" in
        *solaris*)

        # On Solaris (at least, for some versions), libc contains stubbed
        # (non-functional) versions of the pthreads routines, so link-based
        # tests will erroneously succeed.  (We need to link with -pthreads/-mt/
        # -lpthread.)  (The stubs are missing pthread_cleanup_push, or rather
        # a function called by this macro, so we could check for that, but
        # who knows whether they'll stub that too in a future libc.)  So,
        # we'll just look for -pthreads and -lpthread first:

        acx_pthread_flags="-pthreads pthread -mt -pthread $acx_pthread_flags"
        ;;
esac

if test x"$acx_pthread_ok" = xno; then
for flag in $acx_pthread_flags; do

        case $flag in
                none)
                AC_MSG_CHECKING([whether pthreads work without any flags])
                ;;

                -*)
                AC_MSG_CHECKING([whether pthreads work with $flag])
                PTHREAD_CFLAGS="$flag"
                ;;

		pthread-config)
		AC_CHECK_PROG(acx_pthread_config, pthread-config, yes, no)
		if test x"$acx_pthread_config" = xno; then continue; fi
		PTHREAD_CFLAGS="`pthread-config --cflags`"
		PTHREAD_LIBS="`pthread-config --ldflags` `pthread-config --libs`"
		;;

                *)
                AC_MSG_CHECKING([for the pthreads library -l$flag])
                PTHREAD_LIBS="-l$flag"
                ;;
        esac

        save_LIBS="$LIBS"
        save_CFLAGS="$CFLAGS"
        LIBS="$PTHREAD_LIBS $LIBS"
        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"

        # Check for various functions.  We must include pthread.h,
        # since some functions may be macros.  (On the Sequent, we
        # need a special flag -Kthread to make this header compile.)
        # We check for pthread_join because it is in -lpthread on IRIX
        # while pthread_create is in libc.  We check for pthread_attr_init
        # due to DEC craziness with -lpthreads.  We check for
        # pthread_cleanup_push because it is one of the few pthread
        # functions on Solaris that doesn't have a non-functional libc stub.
        # We try pthread_create on general principles.
        AC_TRY_LINK([#include <pthread.h>],
                    [pthread_t th; pthread_join(th, 0);
                     pthread_attr_init(0); pthread_cleanup_push(0, 0);
                     pthread_create(0,0,0,0); pthread_cleanup_pop(0); ],
                    [acx_pthread_ok=yes])

        LIBS="$save_LIBS"
        CFLAGS="$save_CFLAGS"

        AC_MSG_RESULT($acx_pthread_ok)
        if test "x$acx_pthread_ok" = xyes; then
                break;
        fi

        PTHREAD_LIBS=""
        PTHREAD_CFLAGS=""
done
fi

# Various other checks:
if test "x$acx_pthread_ok" = xyes; then
        save_LIBS="$LIBS"
        LIBS="$PTHREAD_LIBS $LIBS"
        save_CFLAGS="$CFLAGS"
        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"

        # Detect AIX lossage: JOINABLE attribute is called UNDETACHED.
	AC_MSG_CHECKING([for joinable pthread attribute])
	attr_name=unknown
	for attr in PTHREAD_CREATE_JOINABLE PTHREAD_CREATE_UNDETACHED; do
	    AC_TRY_LINK([#include <pthread.h>], [int attr=$attr; return attr;],
                        [attr_name=$attr; break])
	done
        AC_MSG_RESULT($attr_name)
        if test "$attr_name" != PTHREAD_CREATE_JOINABLE; then
            AC_DEFINE_UNQUOTED(PTHREAD_CREATE_JOINABLE, $attr_name,
                               [Define to necessary symbol if this constant
                                uses a non-standard name on your system.])
        fi

        AC_MSG_CHECKING([if more special flags are required for pthreads])
        flag=no
        case "${host_cpu}-${host_os}" in
            *-aix* | *-freebsd* | *-darwin*) flag="-D_THREAD_SAFE";;
            *solaris* | *-osf* | *-hpux*) flag="-D_REENTRANT";;
        esac
        AC_MSG_RESULT(${flag})
        if test "x$flag" != xno; then
            PTHREAD_CFLAGS="$flag $PTHREAD_CFLAGS"
        fi

        LIBS="$save_LIBS"
        CFLAGS="$save_CFLAGS"
        # More AIX lossage: must compile with xlc_r or cc_r
	if test x"$GCC" != xyes; then
          AC_CHECK_PROGS(PTHREAD_CC, xlc_r cc_r, ${CC})
        else
          PTHREAD_CC=$CC
	fi

	# The next part tries to detect GCC inconsistency with -shared on some
	# architectures and systems. The problem is that in certain
	# configurations, when -shared is specified, GCC "forgets" to
	# internally use various flags which are still necessary.
	
	#
	# Prepare the flags
	#
	save_CFLAGS="$CFLAGS"
	save_LIBS="$LIBS"
	save_CC="$CC"
	
	# Try with the flags determined by the earlier checks.
	#
	# -Wl,-z,defs forces link-time symbol resolution, so that the
	# linking checks with -shared actually have any value
	#
	# FIXME: -fPIC is required for -shared on many architectures,
	# so we specify it here, but the right way would probably be to
	# properly detect whether it is actually required.
	CFLAGS="-shared -fPIC -Wl,-z,defs $CFLAGS $PTHREAD_CFLAGS"
	LIBS="$PTHREAD_LIBS $LIBS"
	CC="$PTHREAD_CC"
	
	# In order not to create several levels of indentation, we test
	# the value of "$done" until we find the cure or run out of ideas.
	done="no"
	
	# First, make sure the CFLAGS we added are actually accepted by our
	# compiler.  If not (and OS X's ld, for instance, does not accept -z),
	# then we can't do this test.
	if test x"$done" = xno; then
	   AC_MSG_CHECKING([whether to check for GCC pthread/shared inconsistencies])
	   AC_TRY_LINK(,, , [done=yes])
	
	   if test "x$done" = xyes ; then
	      AC_MSG_RESULT([no])
	   else
	      AC_MSG_RESULT([yes])
	   fi
	fi
	
	if test x"$done" = xno; then
	   AC_MSG_CHECKING([whether -pthread is sufficient with -shared])
	   AC_TRY_LINK([#include <pthread.h>],
	      [pthread_t th; pthread_join(th, 0);
	      pthread_attr_init(0); pthread_cleanup_push(0, 0);
	      pthread_create(0,0,0,0); pthread_cleanup_pop(0); ],
	      [done=yes])
	   
	   if test "x$done" = xyes; then
	      AC_MSG_RESULT([yes])
	   else
	      AC_MSG_RESULT([no])
	   fi
	fi
	
	#
	# Linux gcc on some architectures such as mips/mipsel forgets
	# about -lpthread
	#
	if test x"$done" = xno; then
	   AC_MSG_CHECKING([whether -lpthread fixes that])
	   LIBS="-lpthread $PTHREAD_LIBS $save_LIBS"
	   AC_TRY_LINK([#include <pthread.h>],
	      [pthread_t th; pthread_join(th, 0);
	      pthread_attr_init(0); pthread_cleanup_push(0, 0);
	      pthread_create(0,0,0,0); pthread_cleanup_pop(0); ],
	      [done=yes])
	
	   if test "x$done" = xyes; then
	      AC_MSG_RESULT([yes])
	      PTHREAD_LIBS="-lpthread $PTHREAD_LIBS"
	   else
	      AC_MSG_RESULT([no])
	   fi
	fi
	#
	# FreeBSD 4.10 gcc forgets to use -lc_r instead of -lc
	#
	if test x"$done" = xno; then
	   AC_MSG_CHECKING([whether -lc_r fixes that])
	   LIBS="-lc_r $PTHREAD_LIBS $save_LIBS"
	   AC_TRY_LINK([#include <pthread.h>],
	       [pthread_t th; pthread_join(th, 0);
	        pthread_attr_init(0); pthread_cleanup_push(0, 0);
	        pthread_create(0,0,0,0); pthread_cleanup_pop(0); ],
	       [done=yes])
	
	   if test "x$done" = xyes; then
	      AC_MSG_RESULT([yes])
	      PTHREAD_LIBS="-lc_r $PTHREAD_LIBS"
	   else
	      AC_MSG_RESULT([no])
	   fi
	fi
	if test x"$done" = xno; then
	   # OK, we have run out of ideas
	   AC_MSG_WARN([Impossible to determine how to use pthreads with shared libraries])
	
	   # so it's not safe to assume that we may use pthreads
	   acx_pthread_ok=no
	fi
	
	CFLAGS="$save_CFLAGS"
	LIBS="$save_LIBS"
	CC="$save_CC"
else
        PTHREAD_CC="$CC"
fi

AC_SUBST(PTHREAD_LIBS)
AC_SUBST(PTHREAD_CFLAGS)
AC_SUBST(PTHREAD_CC)

# Finally, execute ACTION-IF-FOUND/ACTION-IF-NOT-FOUND:
if test x"$acx_pthread_ok" = xyes; then
        ifelse([$1],,AC_DEFINE(HAVE_PTHREAD,1,[Define if you have POSIX threads libraries and header files.]),[$1])
        :
else
        acx_pthread_ok=no
        $2
fi
AC_LANG_RESTORE
])dnl ACX_PTHREAD
