# SYNOPSIS						-*- Autoconf -*-
#
#  NTP_ENABLE_LOCAL_LIBEVENT
#  NTP_LIBEVENT_CHECK([MINVERSION [, DIR]])
#  NTP_LIBEVENT_CHECK_NOBUILD([MINVERSION [, DIR]])
#
# DESCRIPTION
#
# AUTHOR
#
#  Harlan Stenn
#
# LICENSE
#
#  This file is Copyright (c) 2014 Network Time Foundation
# 
#  Copying and distribution of this file, with or without modification, are
#  permitted in any medium without royalty provided the copyright notice,
#  author attribution and this notice are preserved.  This file is offered
#  as-is, without any warranty.

dnl NTP_ENABLE_LOCAL_LIBEVENT
dnl
dnl Provide only the --enable-local-libevent command-line option.
dnl
dnl Used standalone by top-level NTP configure.ac, which should have
dnl --enable-local-libevent in its --help output but which doesn't do
dnl anything differently based upon its presence or value.
dnl
dnl Also AC_REQUIRE'd by NTP_LIBEVENT_CHECK_NOBUILD.
AC_DEFUN([NTP_ENABLE_LOCAL_LIBEVENT], [

AC_ARG_ENABLE(
    [local-libevent],
    [AC_HELP_STRING(
	[--enable-local-libevent],
	[Force using the supplied libevent tearoff code]
    )],
    [ntp_use_local_libevent=$enableval],
    [ntp_use_local_libevent=${ntp_use_local_libevent-detect}]
)

]) dnl NTP_ENABLE_LOCAL_LIBEVENT

dnl NTP_LIBEVENT_CHECK_NOBUILD([MINVERSION [, DIR]])	     -*- Autoconf -*-
dnl
dnl Look for libevent, which must be at least MINVERSION.
dnl DIR is the location of our "bundled" copy of libevent.
dnl If NOBUILD is provided as the 3rd argument, do all of the above,
dnl but DO NOT invoke DIR/configure if we are going to use our bundled
dnl version.  This may be the case for nested packages.
dnl
dnl provides: --enable-local-libevent 
dnl
dnl Examples:
dnl
dnl    NTP_LIBEVENT_CHECK_NOBUILD([2.0.9], [sntp/libevent])
dnl    NTP_LIBEVENT_CHECK
dnl
AC_DEFUN([NTP_LIBEVENT_CHECK_NOBUILD], [
AC_REQUIRE([NTP_PKG_CONFIG])dnl
AC_REQUIRE([NTP_ENABLE_LOCAL_LIBEVENT])dnl

ntp_libevent_min_version=m4_default([$1], [2.0.9])
ntp_libevent_tearoff=m4_default([$2], [libevent])

AC_SUBST([CFLAGS_LIBEVENT])
AC_SUBST([CPPFLAGS_LIBEVENT])
AC_SUBST([LDADD_LIBEVENT])

case "$ntp_use_local_libevent" in
 yes)
    ;;
 *) # If we have (a good enough) pkg-config, see if it can find libevent
    case "$PKG_CONFIG" in
     /*)
	AC_MSG_CHECKING([if libevent $ntp_libevent_min_version or later is installed])
	if $PKG_CONFIG --atleast-version=$ntp_libevent_min_version libevent
	then
	    ntp_use_local_libevent=no
	    ntp_libevent_version="`$PKG_CONFIG --modversion libevent`"
	    case "$ntp_libevent_version" in
	     *.*) ;;
	     *) ntp_libevent_version='(unknown)' ;;
	    esac
	    AC_MSG_RESULT([yes, version $ntp_libevent_version])
	    CFLAGS_LIBEVENT=`$PKG_CONFIG --cflags libevent_pthreads`
	    CPPFLAGS_LIBEVENT=`$PKG_CONFIG --cflags-only-I libevent`
	    # HMS: I hope the following is accurate.
	    # We don't need -levent, we only need  -levent_core.
	    # While we could grab only the -L stuff, there *might* be
	    # other flags there we want.  Originally we just removed -levent
	    # but then somebody decided to install -levent-2.0
	    # LDADD_LIBEVENT=`$PKG_CONFIG --libs libevent | sed 's:-levent::'`
	    # So now we dance...
	    LDADD_LIBEVENT=
	    for i in `$PKG_CONFIG --libs libevent` `$PKG_CONFIG --cflags-only-other libevent_pthreads`
	    do
		case "$i" in
		 -D*) ;;
		 -levent*) ;;
		 *) case "$LDADD_LIBEVENT" in
		     '') LDADD_LIBEVENT="$i" ;;
		     *) LDADD_LIBEVENT="$LDADD_LIBEVENT $i" ;;
		    esac
		    ;;
		esac
	    done
	    case "$LIBISC_PTHREADS_NOTHREADS" in
	     pthreads)
		LDADD_LIBEVENT="$LDADD_LIBEVENT -levent_pthreads"
	    esac
	    LDADD_LIBEVENT="$LDADD_LIBEVENT -levent_core"
	else
	    ntp_use_local_libevent=yes
	    # HMS: do we only need to do this if LIBISC_PTHREADS_NOTHREADS
	    # is "pthreads"?
	    CFLAGS_LIBEVENT=`$PKG_CONFIG --cflags libevent_pthreads`
	    AC_MSG_RESULT([no])
	fi
	;;
     *)
	ntp_use_local_libevent=yes
	;;
    esac
    ;;
esac

case "$ntp_use_local_libevent" in
 yes)
    AC_MSG_NOTICE([Using libevent tearoff])
    CPPFLAGS_LIBEVENT="-I\$(top_builddir)/$ntp_libevent_tearoff/include -I\$(top_srcdir)/$ntp_libevent_tearoff/include"
    case "$LIBISC_PTHREADS_NOTHREADS" in
     pthreads)
	LDADD_LIBEVENT="\$(top_builddir)/$ntp_libevent_tearoff/libevent_pthreads.la \$(top_builddir)/$ntp_libevent_tearoff/libevent_core.la"
	;;
     *)
	LDADD_LIBEVENT="\$(top_builddir)/$ntp_libevent_tearoff/libevent_core.la"
    esac
esac

dnl AC_ARG_ENABLE(
dnl     [cflags-libevent],
dnl     [AC_HELP_STRING(
dnl 	[--enable-cflags-libevent=-pthread],
dnl 	[CFLAGS value to build with pthreads]
dnl     )],
dnl     [CFLAGS_LIBEVENT=$enableval],
dnl     [# See above about LIBISC_PTHREADS_NOTHREADS
dnl     case "$CFLAGS_LIBEVENT" in
dnl      '') CFLAGS_LIBEVENT="-pthread" ;;	
dnl      *) ;;
dnl     esac]
dnl )
dnl AC_MSG_NOTICE([LIBISC_PTHREADS_NOTHREADS is <$LIBISC_PTHREADS_NOTHREADS>])
dnl AC_MSG_NOTICE([CFLAGS_LIBEVENT is <$CFLAGS_LIBEVENT>])

AM_CONDITIONAL([BUILD_LIBEVENT], [test "x$ntp_use_local_libevent" = "xyes"])

]) dnl NTP_LIBEVENT_CHECK_NOBUILD

dnl NTP_LIBEVENT_CHECK([MINVERSION [, DIR]])	     -*- Autoconf -*-
AC_DEFUN([NTP_LIBEVENT_CHECK], [

AC_SUBST([NTP_FORCE_LIBEVENT_DIST])
NTP_LIBEVENT_CHECK_NOBUILD([$1], [$2])

case "$ntp_libevent_tearoff" in
 libevent)
    ;;
 *)
    AC_MSG_ERROR([ntp_libevent.m4 dir must be libevent, not $ntp_libevent_tearoff])
    ;;
esac

case "$ntp_use_local_libevent" in
 yes)
    dnl ac_configure_args is undocumented but widely abused, as here,
    dnl to modify the defaults of the libevent subpackage, by prefixing
    dnl our changes to the child configure arguments already assembled.
    dnl User-supplied contradictory choices should prevail thanks to
    dnl "last wins".
    ac_configure_args=" --disable-openssl${ac_configure_args}"
    ac_configure_args=" --disable-shared${ac_configure_args}"
    ac_configure_args=" --disable-libevent-regress${ac_configure_args}"
    ac_configure_args=" --disable-libevent-install${ac_configure_args}"
    ac_configure_args=" --enable-silent-rules${ac_configure_args}"
    ac_configure_args=" --enable-function-sections${ac_configure_args}"
    ac_configure_args=" LEP_CFLAGS='${NTP_HARD_CFLAGS}'${ac_configure_args}"
    ac_configure_args=" LEP_CPPFLAGS='${NTP_HARD_CPPFLAGS}'${ac_configure_args}"
    ac_configure_args=" LEP_LDFLAGS='${NTP_HARD_LDFLAGS}'${ac_configure_args}"
    AC_CONFIG_SUBDIRS([libevent])
    ;;
 *)
    NTP_FORCE_LIBEVENT_DIST=libevent
    ;;
esac

]) dnl NTP_LIBEVENT_CHECK

