#
# Copyright (c) 1999-2004 Damien Miller
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

AC_INIT([OpenSSH], [Portable], [openssh-unix-dev@mindrot.org])
AC_REVISION($Revision: 1.583 $)
AC_CONFIG_SRCDIR([ssh.c])
AC_LANG([C])

AC_CONFIG_HEADER([config.h])
AC_PROG_CC
AC_CANONICAL_HOST
AC_C_BIGENDIAN

# Checks for programs.
AC_PROG_AWK
AC_PROG_CPP
AC_PROG_RANLIB
AC_PROG_INSTALL
AC_PROG_EGREP
AC_PROG_MKDIR_P
AC_CHECK_TOOLS([AR], [ar])
AC_PATH_PROG([CAT], [cat])
AC_PATH_PROG([KILL], [kill])
AC_PATH_PROG([SED], [sed])
AC_PATH_PROG([ENT], [ent])
AC_SUBST([ENT])
AC_PATH_PROG([TEST_MINUS_S_SH], [bash])
AC_PATH_PROG([TEST_MINUS_S_SH], [ksh])
AC_PATH_PROG([TEST_MINUS_S_SH], [sh])
AC_PATH_PROG([SH], [sh])
AC_PATH_PROG([GROFF], [groff])
AC_PATH_PROG([NROFF], [nroff])
AC_PATH_PROG([MANDOC], [mandoc])
AC_SUBST([TEST_SHELL], [sh])

dnl select manpage formatter
if test "x$MANDOC" != "x" ; then
	MANFMT="$MANDOC"
elif test "x$NROFF" != "x" ; then
	MANFMT="$NROFF -mandoc"
elif test "x$GROFF" != "x" ; then
	MANFMT="$GROFF -mandoc -Tascii"
else
	AC_MSG_WARN([no manpage formatted found])
	MANFMT="false"
fi
AC_SUBST([MANFMT])

dnl for buildpkg.sh
AC_PATH_PROG([PATH_GROUPADD_PROG], [groupadd], [groupadd],
	[/usr/sbin${PATH_SEPARATOR}/etc])
AC_PATH_PROG([PATH_USERADD_PROG], [useradd], [useradd],
	[/usr/sbin${PATH_SEPARATOR}/etc])
AC_CHECK_PROG([MAKE_PACKAGE_SUPPORTED], [pkgmk], [yes], [no])
if test -x /sbin/sh; then
	AC_SUBST([STARTUP_SCRIPT_SHELL], [/sbin/sh])
else
	AC_SUBST([STARTUP_SCRIPT_SHELL], [/bin/sh])
fi

# System features
AC_SYS_LARGEFILE

if test -z "$AR" ; then
	AC_MSG_ERROR([*** 'ar' missing, please install or fix your \$PATH ***])
fi

AC_PATH_PROG([PATH_PASSWD_PROG], [passwd])
if test ! -z "$PATH_PASSWD_PROG" ; then
	AC_DEFINE_UNQUOTED([_PATH_PASSWD_PROG], ["$PATH_PASSWD_PROG"],
		[Full path of your "passwd" program])
fi

dnl Since autoconf doesn't support it very well,  we no longer allow users to
dnl override LD, however keeping the hook here for now in case there's a use
dnl use case we overlooked and someone needs to re-enable it.  Unless a good
dnl reason is found we'll be removing this in future.
LD="$CC"
AC_SUBST([LD])

AC_C_INLINE

AC_CHECK_DECL([LLONG_MAX], [have_llong_max=1], , [#include <limits.h>])
AC_CHECK_DECL([SYSTR_POLICY_KILL], [have_systr_policy_kill=1], , [
	#include <sys/types.h>
	#include <sys/param.h>
	#include <dev/systrace.h>
])
AC_CHECK_DECL([RLIMIT_NPROC],
    [AC_DEFINE([HAVE_RLIMIT_NPROC], [], [sys/resource.h has RLIMIT_NPROC])], , [
	#include <sys/types.h>
	#include <sys/resource.h>
])
AC_CHECK_DECL([PR_SET_NO_NEW_PRIVS], [have_linux_no_new_privs=1], , [
	#include <sys/types.h>
	#include <linux/prctl.h>
])

openssl=yes
AC_ARG_WITH([openssl],
	[  --without-openssl       Disable use of OpenSSL; use only limited internal crypto **EXPERIMENTAL** ],
	[  if test "x$withval" = "xno" ; then
		openssl=no
	   fi
	]
)
AC_MSG_CHECKING([whether OpenSSL will be used for cryptography])
if test "x$openssl" = "xyes" ; then
	AC_MSG_RESULT([yes])
	AC_DEFINE_UNQUOTED([WITH_OPENSSL], [1], [use libcrypto for cryptography])
else
	AC_MSG_RESULT([no])
fi

use_stack_protector=1
use_toolchain_hardening=1
AC_ARG_WITH([stackprotect],
    [  --without-stackprotect  Don't use compiler's stack protection], [
    if test "x$withval" = "xno"; then
	use_stack_protector=0
    fi ])
AC_ARG_WITH([hardening],
    [  --without-hardening     Don't use toolchain hardening flags], [
    if test "x$withval" = "xno"; then
	use_toolchain_hardening=0
    fi ])

# We use -Werror for the tests only so that we catch warnings like "this is
# on by default" for things like -fPIE.
AC_MSG_CHECKING([if $CC supports -Werror])
saved_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Werror"
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[int main(void) { return 0; }]])],
	[ AC_MSG_RESULT([yes])
	  WERROR="-Werror"],
	[ AC_MSG_RESULT([no])
	  WERROR="" ]
)
CFLAGS="$saved_CFLAGS"

if test "$GCC" = "yes" || test "$GCC" = "egcs"; then
	OSSH_CHECK_CFLAG_COMPILE([-pipe])
	OSSH_CHECK_CFLAG_COMPILE([-Qunused-arguments])
	OSSH_CHECK_CFLAG_COMPILE([-Wunknown-warning-option])
	OSSH_CHECK_CFLAG_COMPILE([-Wall])
	OSSH_CHECK_CFLAG_COMPILE([-Wpointer-arith])
	OSSH_CHECK_CFLAG_COMPILE([-Wuninitialized])
	OSSH_CHECK_CFLAG_COMPILE([-Wsign-compare])
	OSSH_CHECK_CFLAG_COMPILE([-Wformat-security])
	OSSH_CHECK_CFLAG_COMPILE([-Wsizeof-pointer-memaccess])
	OSSH_CHECK_CFLAG_COMPILE([-Wpointer-sign], [-Wno-pointer-sign])
	OSSH_CHECK_CFLAG_COMPILE([-Wunused-result], [-Wno-unused-result])
	OSSH_CHECK_CFLAG_COMPILE([-fno-strict-aliasing])
    if test "x$use_toolchain_hardening" = "x1"; then
	# Cygwin GCC 7.x allows thunking on the CLI, but produces non-working
	# code.  Unfortunately you only notice this at link time.
	case "$host" in
	*-*-cygwin*) ;;
	*)
	    OSSH_CHECK_CFLAG_COMPILE([-mfunction-return=thunk]) # gcc
	    OSSH_CHECK_CFLAG_COMPILE([-mindirect-branch=thunk]) # gcc
	    ;;
	esac
	OSSH_CHECK_CFLAG_COMPILE([-mretpoline]) # clang
	OSSH_CHECK_LDFLAG_LINK([-Wl,-z,retpolineplt])
	OSSH_CHECK_CFLAG_COMPILE([-D_FORTIFY_SOURCE=2])
	OSSH_CHECK_LDFLAG_LINK([-Wl,-z,relro])
	OSSH_CHECK_LDFLAG_LINK([-Wl,-z,now])
	OSSH_CHECK_LDFLAG_LINK([-Wl,-z,noexecstack])
	# NB. -ftrapv expects certain support functions to be present in
	# the compiler library (libgcc or similar) to detect integer operations
	# that can overflow. We must check that the result of enabling it
	# actually links. The test program compiled/linked includes a number
	# of integer operations that should exercise this.
	OSSH_CHECK_CFLAG_LINK([-ftrapv])
    fi
	AC_MSG_CHECKING([gcc version])
	GCC_VER=`$CC -v 2>&1 | $AWK '/gcc version /{print $3}'`
	case $GCC_VER in
		1.*) no_attrib_nonnull=1 ;;
		2.8* | 2.9*)
		     no_attrib_nonnull=1
		     ;;
		2.*) no_attrib_nonnull=1 ;;
		*) ;;
	esac
	AC_MSG_RESULT([$GCC_VER])

	AC_MSG_CHECKING([if $CC accepts -fno-builtin-memset])
	saved_CFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS -fno-builtin-memset"
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[ #include <string.h> ]],
			[[ char b[10]; memset(b, 0, sizeof(b)); ]])],
		[ AC_MSG_RESULT([yes]) ],
		[ AC_MSG_RESULT([no])
		  CFLAGS="$saved_CFLAGS" ]
	)

	# -fstack-protector-all doesn't always work for some GCC versions
	# and/or platforms, so we test if we can.  If it's not supported
	# on a given platform gcc will emit a warning so we use -Werror.
	if test "x$use_stack_protector" = "x1"; then
	    for t in -fstack-protector-strong -fstack-protector-all \
		    -fstack-protector; do
		AC_MSG_CHECKING([if $CC supports $t])
		saved_CFLAGS="$CFLAGS"
		saved_LDFLAGS="$LDFLAGS"
		CFLAGS="$CFLAGS $t -Werror"
		LDFLAGS="$LDFLAGS $t -Werror"
		AC_LINK_IFELSE(
			[AC_LANG_PROGRAM([[ #include <stdio.h> ]],
			[[
	char x[256];
	snprintf(x, sizeof(x), "XXX");
			 ]])],
		    [ AC_MSG_RESULT([yes])
		      CFLAGS="$saved_CFLAGS $t"
		      LDFLAGS="$saved_LDFLAGS $t"
		      AC_MSG_CHECKING([if $t works])
		      AC_RUN_IFELSE(
			[AC_LANG_PROGRAM([[ #include <stdio.h> ]],
			[[
	char x[256];
	snprintf(x, sizeof(x), "XXX");
			]])],
			[ AC_MSG_RESULT([yes])
			  break ],
			[ AC_MSG_RESULT([no]) ],
			[ AC_MSG_WARN([cross compiling: cannot test])
			  break ]
		      )
		    ],
		    [ AC_MSG_RESULT([no]) ]
		)
		CFLAGS="$saved_CFLAGS"
		LDFLAGS="$saved_LDFLAGS"
	    done
	fi

	if test -z "$have_llong_max"; then
		# retry LLONG_MAX with -std=gnu99, needed on some Linuxes
		unset ac_cv_have_decl_LLONG_MAX
		saved_CFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS -std=gnu99"
		AC_CHECK_DECL([LLONG_MAX],
		    [have_llong_max=1],
		    [CFLAGS="$saved_CFLAGS"],
		    [#include <limits.h>]
		)
	fi
fi

AC_MSG_CHECKING([if compiler allows __attribute__ on return types])
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[
#include <stdlib.h>
__attribute__((__unused__)) static void foo(void){return;}]],
    [[ exit(0); ]])],
    [ AC_MSG_RESULT([yes]) ],
    [ AC_MSG_RESULT([no])
      AC_DEFINE(NO_ATTRIBUTE_ON_RETURN_TYPE, 1,
	 [compiler does not accept __attribute__ on return types]) ]
)

AC_MSG_CHECKING([if compiler allows __attribute__ prototype args])
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[
#include <stdlib.h>
typedef void foo(const char *, ...) __attribute__((format(printf, 1, 2)));]],
    [[ exit(0); ]])],
    [ AC_MSG_RESULT([yes]) ],
    [ AC_MSG_RESULT([no])
      AC_DEFINE(NO_ATTRIBUTE_ON_PROTOTYPE_ARGS, 1,
	 [compiler does not accept __attribute__ on prototype args]) ]
)

if test "x$no_attrib_nonnull" != "x1" ; then
	AC_DEFINE([HAVE_ATTRIBUTE__NONNULL__], [1], [Have attribute nonnull])
fi

AC_ARG_WITH([rpath],
	[  --without-rpath         Disable auto-added -R linker paths],
	[
		if test "x$withval" = "xno" ; then
			need_dash_r=""
		fi
		if test "x$withval" = "xyes" ; then
			need_dash_r=1
		fi
	]
)

# Allow user to specify flags
AC_ARG_WITH([cflags],
	[  --with-cflags           Specify additional flags to pass to compiler],
	[
		if test -n "$withval"  &&  test "x$withval" != "xno"  &&  \
		    test "x${withval}" != "xyes"; then
			CFLAGS="$CFLAGS $withval"
		fi
	]
)

AC_ARG_WITH([cflags-after],
	[  --with-cflags-after     Specify additional flags to pass to compiler after configure],
	[
		if test -n "$withval"  &&  test "x$withval" != "xno"  &&  \
		    test "x${withval}" != "xyes"; then
			CFLAGS_AFTER="$withval"
		fi
	]
)
AC_ARG_WITH([cppflags],
	[  --with-cppflags         Specify additional flags to pass to preprocessor] ,
	[
		if test -n "$withval"  &&  test "x$withval" != "xno"  &&  \
		    test "x${withval}" != "xyes"; then
			CPPFLAGS="$CPPFLAGS $withval"
		fi
	]
)
AC_ARG_WITH([ldflags],
	[  --with-ldflags          Specify additional flags to pass to linker],
	[
		if test -n "$withval"  &&  test "x$withval" != "xno"  &&  \
		    test "x${withval}" != "xyes"; then
			LDFLAGS="$LDFLAGS $withval"
		fi
	]
)
AC_ARG_WITH([ldflags-after],
	[  --with-ldflags-after    Specify additional flags to pass to linker after configure],
	[
		if test -n "$withval"  &&  test "x$withval" != "xno"  &&  \
		    test "x${withval}" != "xyes"; then
			LDFLAGS_AFTER="$withval"
		fi
	]
)
AC_ARG_WITH([libs],
	[  --with-libs             Specify additional libraries to link with],
	[
		if test -n "$withval"  &&  test "x$withval" != "xno"  &&  \
		    test "x${withval}" != "xyes"; then
			LIBS="$LIBS $withval"
		fi
	]
)
AC_ARG_WITH([Werror],
	[  --with-Werror           Build main code with -Werror],
	[
		if test -n "$withval"  &&  test "x$withval" != "xno"; then
			werror_flags="-Werror"
			if test "x${withval}" != "xyes"; then
				werror_flags="$withval"
			fi
		fi
	]
)

AC_CHECK_HEADERS([ \
	blf.h \
	bstring.h \
	crypt.h \
	crypto/sha2.h \
	dirent.h \
	endian.h \
	elf.h \
	err.h \
	features.h \
	fcntl.h \
	floatingpoint.h \
	getopt.h \
	glob.h \
	ia.h \
	iaf.h \
	ifaddrs.h \
	inttypes.h \
	langinfo.h \
	limits.h \
	locale.h \
	login.h \
	maillock.h \
	ndir.h \
	net/if_tun.h \
	netdb.h \
	netgroup.h \
	pam/pam_appl.h \
	paths.h \
	poll.h \
	pty.h \
	readpassphrase.h \
	rpc/types.h \
	security/pam_appl.h \
	sha2.h \
	shadow.h \
	stddef.h \
	stdint.h \
	string.h \
	strings.h \
	sys/bitypes.h \
	sys/bsdtty.h \
	sys/cdefs.h \
	sys/dir.h \
	sys/file.h \
	sys/mman.h \
	sys/label.h \
	sys/ndir.h \
	sys/poll.h \
	sys/prctl.h \
	sys/pstat.h \
	sys/ptrace.h \
	sys/random.h \
	sys/select.h \
	sys/stat.h \
	sys/stream.h \
	sys/stropts.h \
	sys/strtio.h \
	sys/statvfs.h \
	sys/sysmacros.h \
	sys/time.h \
	sys/timers.h \
	sys/vfs.h \
	time.h \
	tmpdir.h \
	ttyent.h \
	ucred.h \
	unistd.h \
	usersec.h \
	util.h \
	utime.h \
	utmp.h \
	utmpx.h \
	vis.h \
	wchar.h \
])

# On some platforms (eg SunOS4) sys/audit.h requires sys/[time|types|label.h]
# to be included first.
AC_CHECK_HEADERS([sys/audit.h], [], [], [
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_LABEL_H
# include <sys/label.h>
#endif
])

# sys/capsicum.h requires sys/types.h
AC_CHECK_HEADERS([sys/capsicum.h], [], [], [
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
])

# net/route.h requires sys/socket.h and sys/types.h.
# sys/sysctl.h also requires sys/param.h
AC_CHECK_HEADERS([net/route.h sys/sysctl.h], [], [], [
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#include <sys/param.h>
#include <sys/socket.h>
])

# lastlog.h requires sys/time.h to be included first on Solaris
AC_CHECK_HEADERS([lastlog.h], [], [], [
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
])

# sys/ptms.h requires sys/stream.h to be included first on Solaris
AC_CHECK_HEADERS([sys/ptms.h], [], [], [
#ifdef HAVE_SYS_STREAM_H
# include <sys/stream.h>
#endif
])

# login_cap.h requires sys/types.h on NetBSD
AC_CHECK_HEADERS([login_cap.h], [], [], [
#include <sys/types.h>
])

# older BSDs need sys/param.h before sys/mount.h
AC_CHECK_HEADERS([sys/mount.h], [], [], [
#include <sys/param.h>
])

# Android requires sys/socket.h to be included before sys/un.h
AC_CHECK_HEADERS([sys/un.h], [], [], [
#include <sys/types.h>
#include <sys/socket.h>
])

# Messages for features tested for in target-specific section
SIA_MSG="no"
SPC_MSG="no"
SP_MSG="no"
SPP_MSG="no"

# Support for Solaris/Illumos privileges (this test is used by both
# the --with-solaris-privs option and --with-sandbox=solaris).
SOLARIS_PRIVS="no"

# Check for some target-specific stuff
case "$host" in
*-*-aix*)
	# Some versions of VAC won't allow macro redefinitions at
	# -qlanglevel=ansi, and autoconf 2.60 sometimes insists on using that
	# particularly with older versions of vac or xlc.
	# It also throws errors about null macro arguments, but these are
	# not fatal.
	AC_MSG_CHECKING([if compiler allows macro redefinitions])
	AC_COMPILE_IFELSE(
	    [AC_LANG_PROGRAM([[
#define testmacro foo
#define testmacro bar]],
	    [[ exit(0); ]])],
	    [ AC_MSG_RESULT([yes]) ],
	    [ AC_MSG_RESULT([no])
	      CC="`echo $CC | sed 's/-qlanglvl\=ansi//g'`"
	      CFLAGS="`echo $CFLAGS | sed 's/-qlanglvl\=ansi//g'`"
	      CPPFLAGS="`echo $CPPFLAGS | sed 's/-qlanglvl\=ansi//g'`"
	    ]
	)

	AC_MSG_CHECKING([how to specify blibpath for linker ($LD)])
	if (test -z "$blibpath"); then
		blibpath="/usr/lib:/lib"
	fi
	saved_LDFLAGS="$LDFLAGS"
	if test "$GCC" = "yes"; then
		flags="-Wl,-blibpath: -Wl,-rpath, -blibpath:"
	else
		flags="-blibpath: -Wl,-blibpath: -Wl,-rpath,"
	fi
	for tryflags in $flags ;do
		if (test -z "$blibflags"); then
			LDFLAGS="$saved_LDFLAGS $tryflags$blibpath"
			AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[]])],
			[blibflags=$tryflags], [])
		fi
	done
	if (test -z "$blibflags"); then
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([*** must be able to specify blibpath on AIX - check config.log])
	else
		AC_MSG_RESULT([$blibflags])
	fi
	LDFLAGS="$saved_LDFLAGS"
	dnl Check for authenticate.  Might be in libs.a on older AIXes
	AC_CHECK_FUNC([authenticate], [AC_DEFINE([WITH_AIXAUTHENTICATE], [1],
		[Define if you want to enable AIX4's authenticate function])],
		[AC_CHECK_LIB([s], [authenticate],
			[ AC_DEFINE([WITH_AIXAUTHENTICATE])
				LIBS="$LIBS -ls"
			])
		])
	dnl Check for various auth function declarations in headers.
	AC_CHECK_DECLS([authenticate, loginrestrictions, loginsuccess,
	    passwdexpired, setauthdb], , , [#include <usersec.h>])
	dnl Check if loginfailed is declared and takes 4 arguments (AIX >= 5.2)
	AC_CHECK_DECLS([loginfailed],
	    [AC_MSG_CHECKING([if loginfailed takes 4 arguments])
	    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <usersec.h> ]],
		[[ (void)loginfailed("user","host","tty",0); ]])],
		[AC_MSG_RESULT([yes])
		AC_DEFINE([AIX_LOGINFAILED_4ARG], [1],
			[Define if your AIX loginfailed() function
			takes 4 arguments (AIX >= 5.2)])], [AC_MSG_RESULT([no])
	    ])],
	    [],
	    [#include <usersec.h>]
	)
	AC_CHECK_FUNCS([getgrset setauthdb])
	AC_CHECK_DECL([F_CLOSEM],
	    AC_DEFINE([HAVE_FCNTL_CLOSEM], [1], [Use F_CLOSEM fcntl for closefrom]),
	    [],
	    [ #include <limits.h>
	      #include <fcntl.h> ]
	)
	check_for_aix_broken_getaddrinfo=1
	AC_DEFINE([BROKEN_REALPATH], [1], [Define if you have a broken realpath.])
	AC_DEFINE([SETEUID_BREAKS_SETUID], [1],
	    [Define if your platform breaks doing a seteuid before a setuid])
	AC_DEFINE([BROKEN_SETREUID], [1], [Define if your setreuid() is broken])
	AC_DEFINE([BROKEN_SETREGID], [1], [Define if your setregid() is broken])
	dnl AIX handles lastlog as part of its login message
	AC_DEFINE([DISABLE_LASTLOG], [1], [Define if you don't want to use lastlog])
	AC_DEFINE([LOGIN_NEEDS_UTMPX], [1],
		[Some systems need a utmpx entry for /bin/login to work])
	AC_DEFINE([SPT_TYPE], [SPT_REUSEARGV],
		[Define to a Set Process Title type if your system is
		supported by bsd-setproctitle.c])
	AC_DEFINE([SSHPAM_CHAUTHTOK_NEEDS_RUID], [1],
	    [AIX 5.2 and 5.3 (and presumably newer) require this])
	AC_DEFINE([PTY_ZEROREAD], [1], [read(1) can return 0 for a non-closed fd])
	AC_DEFINE([PLATFORM_SYS_DIR_UID], 2, [System dirs owned by bin (uid 2)])
	AC_DEFINE([BROKEN_STRNDUP], 1, [strndup broken, see APAR IY61211])
	AC_DEFINE([BROKEN_STRNLEN], 1, [strnlen broken, see APAR IY62551])
	;;
*-*-android*)
	AC_DEFINE([DISABLE_UTMP], [1], [Define if you don't want to use utmp])
	AC_DEFINE([DISABLE_WTMP], [1], [Define if you don't want to use wtmp])
	;;
*-*-cygwin*)
	check_for_libcrypt_later=1
	LIBS="$LIBS /usr/lib/textreadmode.o"
	AC_DEFINE([HAVE_CYGWIN], [1], [Define if you are on Cygwin])
	AC_DEFINE([USE_PIPES], [1], [Use PIPES instead of a socketpair()])
	AC_DEFINE([NO_UID_RESTORATION_TEST], [1],
		[Define to disable UID restoration test])
	AC_DEFINE([DISABLE_SHADOW], [1],
		[Define if you want to disable shadow passwords])
	AC_DEFINE([NO_X11_UNIX_SOCKETS], [1],
		[Define if X11 doesn't support AF_UNIX sockets on that system])
	AC_DEFINE([DISABLE_FD_PASSING], [1],
		[Define if your platform needs to skip post auth
		file descriptor passing])
	AC_DEFINE([SSH_IOBUFSZ], [65535], [Windows is sensitive to read buffer size])
	AC_DEFINE([FILESYSTEM_NO_BACKSLASH], [1], [File names may not contain backslash characters])
	# Cygwin defines optargs, optargs as declspec(dllimport) for historical
	# reasons which cause compile warnings, so we disable those warnings.
	OSSH_CHECK_CFLAG_COMPILE([-Wno-attributes])
	;;
*-*-dgux*)
	AC_DEFINE([IP_TOS_IS_BROKEN], [1],
		[Define if your system choked on IP TOS setting])
	AC_DEFINE([SETEUID_BREAKS_SETUID])
	AC_DEFINE([BROKEN_SETREUID])
	AC_DEFINE([BROKEN_SETREGID])
	;;
*-*-darwin*)
	use_pie=auto
	AC_MSG_CHECKING([if we have working getaddrinfo])
	AC_RUN_IFELSE([AC_LANG_SOURCE([[ #include <mach-o/dyld.h>
main() { if (NSVersionOfRunTimeLibrary("System") >= (60 << 16))
		exit(0);
	else
		exit(1);
}
			]])],
	[AC_MSG_RESULT([working])],
	[AC_MSG_RESULT([buggy])
	AC_DEFINE([BROKEN_GETADDRINFO], [1],
		[getaddrinfo is broken (if present)])
	],
	[AC_MSG_RESULT([assume it is working])])
	AC_DEFINE([SETEUID_BREAKS_SETUID])
	AC_DEFINE([BROKEN_SETREUID])
	AC_DEFINE([BROKEN_SETREGID])
	AC_DEFINE([BROKEN_GLOB], [1], [OS X glob does not do what we expect])
	AC_DEFINE_UNQUOTED([BIND_8_COMPAT], [1],
		[Define if your resolver libs need this for getrrsetbyname])
	AC_DEFINE([SSH_TUN_FREEBSD], [1], [Open tunnel devices the FreeBSD way])
	AC_DEFINE([SSH_TUN_COMPAT_AF], [1],
	    [Use tunnel device compatibility to OpenBSD])
	AC_DEFINE([SSH_TUN_PREPEND_AF], [1],
	    [Prepend the address family to IP tunnel traffic])
	m4_pattern_allow([AU_IPv])
	AC_CHECK_DECL([AU_IPv4], [],
	    AC_DEFINE([AU_IPv4], [0], [System only supports IPv4 audit records])
	    [#include <bsm/audit.h>]
	AC_DEFINE([LASTLOG_WRITE_PUTUTXLINE], [1],
	    [Define if pututxline updates lastlog too])
	)
	AC_DEFINE([SPT_TYPE], [SPT_REUSEARGV],
		[Define to a Set Process Title type if your system is
		supported by bsd-setproctitle.c])
	AC_CHECK_FUNCS([sandbox_init])
	AC_CHECK_HEADERS([sandbox.h])
	AC_CHECK_LIB([sandbox], [sandbox_apply], [
	    SSHDLIBS="$SSHDLIBS -lsandbox"
	])
	;;
*-*-dragonfly*)
	SSHDLIBS="$SSHDLIBS -lcrypt"
	TEST_MALLOC_OPTIONS="AFGJPRX"
	;;
*-*-haiku*)
	LIBS="$LIBS -lbsd "
	AC_CHECK_LIB([network], [socket])
	AC_DEFINE([HAVE_U_INT64_T])
	MANTYPE=man
	;;
*-*-hpux*)
	# first we define all of the options common to all HP-UX releases
	CPPFLAGS="$CPPFLAGS -D_HPUX_SOURCE -D_XOPEN_SOURCE -D_XOPEN_SOURCE_EXTENDED=1"
	IPADDR_IN_DISPLAY=yes
	AC_DEFINE([USE_PIPES])
	AC_DEFINE([LOGIN_NEEDS_UTMPX])
	AC_DEFINE([LOCKED_PASSWD_STRING], ["*"],
		[String used in /etc/passwd to denote locked account])
	AC_DEFINE([SPT_TYPE], [SPT_PSTAT])
	AC_DEFINE([PLATFORM_SYS_DIR_UID], 2, [System dirs owned by bin (uid 2)])
	maildir="/var/mail"
	LIBS="$LIBS -lsec"
	AC_CHECK_LIB([xnet], [t_error], ,
	    [AC_MSG_ERROR([*** -lxnet needed on HP-UX - check config.log ***])])

	# next, we define all of the options specific to major releases
	case "$host" in
	*-*-hpux10*)
		if test -z "$GCC"; then
			CFLAGS="$CFLAGS -Ae"
		fi
		;;
	*-*-hpux11*)
		AC_DEFINE([PAM_SUN_CODEBASE], [1],
			[Define if you are using Solaris-derived PAM which
			passes pam_messages to the conversation function
			with an extra level of indirection])
		AC_DEFINE([DISABLE_UTMP], [1],
			[Define if you don't want to use utmp])
		AC_DEFINE([USE_BTMP], [1], [Use btmp to log bad logins])
		check_for_hpux_broken_getaddrinfo=1
		check_for_conflicting_getspnam=1
		;;
	esac

	# lastly, we define options specific to minor releases
	case "$host" in
	*-*-hpux10.26)
		AC_DEFINE([HAVE_SECUREWARE], [1],
			[Define if you have SecureWare-based
			protected password database])
		disable_ptmx_check=yes
		LIBS="$LIBS -lsecpw"
		;;
	esac
	;;
*-*-irix5*)
	PATH="$PATH:/usr/etc"
	AC_DEFINE([BROKEN_INET_NTOA], [1],
		[Define if you system's inet_ntoa is busted
		(e.g. Irix gcc issue)])
	AC_DEFINE([SETEUID_BREAKS_SETUID])
	AC_DEFINE([BROKEN_SETREUID])
	AC_DEFINE([BROKEN_SETREGID])
	AC_DEFINE([WITH_ABBREV_NO_TTY], [1],
		[Define if you shouldn't strip 'tty' from your
		ttyname in [uw]tmp])
	AC_DEFINE([LOCKED_PASSWD_STRING], ["*LK*"])
	;;
*-*-irix6*)
	PATH="$PATH:/usr/etc"
	AC_DEFINE([WITH_IRIX_ARRAY], [1],
		[Define if you have/want arrays
		(cluster-wide session management, not C arrays)])
	AC_DEFINE([WITH_IRIX_PROJECT], [1],
		[Define if you want IRIX project management])
	AC_DEFINE([WITH_IRIX_AUDIT], [1],
		[Define if you want IRIX audit trails])
	AC_CHECK_FUNC([jlimit_startjob], [AC_DEFINE([WITH_IRIX_JOBS], [1],
		[Define if you want IRIX kernel jobs])])
	AC_DEFINE([BROKEN_INET_NTOA])
	AC_DEFINE([SETEUID_BREAKS_SETUID])
	AC_DEFINE([BROKEN_SETREUID])
	AC_DEFINE([BROKEN_SETREGID])
	AC_DEFINE([BROKEN_UPDWTMPX], [1], [updwtmpx is broken (if present)])
	AC_DEFINE([WITH_ABBREV_NO_TTY])
	AC_DEFINE([LOCKED_PASSWD_STRING], ["*LK*"])
	;;
*-*-k*bsd*-gnu | *-*-kopensolaris*-gnu)
	check_for_libcrypt_later=1
	AC_DEFINE([PAM_TTY_KLUDGE])
	AC_DEFINE([LOCKED_PASSWD_PREFIX], ["!"])
	AC_DEFINE([SPT_TYPE], [SPT_REUSEARGV])
	AC_DEFINE([_PATH_BTMP], ["/var/log/btmp"], [log for bad login attempts])
	AC_DEFINE([USE_BTMP], [1], [Use btmp to log bad logins])
	;;
*-*-linux*)
	no_dev_ptmx=1
	use_pie=auto
	check_for_libcrypt_later=1
	check_for_openpty_ctty_bug=1
	dnl Target SUSv3/POSIX.1-2001 plus BSD specifics.
	dnl _DEFAULT_SOURCE is the new name for _BSD_SOURCE
	CPPFLAGS="$CPPFLAGS -D_XOPEN_SOURCE=600 -D_BSD_SOURCE -D_DEFAULT_SOURCE"
	AC_DEFINE([PAM_TTY_KLUDGE], [1],
		[Work around problematic Linux PAM modules handling of PAM_TTY])
	AC_DEFINE([LOCKED_PASSWD_PREFIX], ["!"],
		[String used in /etc/passwd to denote locked account])
	AC_DEFINE([SPT_TYPE], [SPT_REUSEARGV])
	AC_DEFINE([LINK_OPNOTSUPP_ERRNO], [EPERM],
		[Define to whatever link() returns for "not supported"
		if it doesn't return EOPNOTSUPP.])
	AC_DEFINE([_PATH_BTMP], ["/var/log/btmp"], [log for bad login attempts])
	AC_DEFINE([USE_BTMP])
	AC_DEFINE([LINUX_OOM_ADJUST], [1], [Adjust Linux out-of-memory killer])
	inet6_default_4in6=yes
	case `uname -r` in
	1.*|2.0.*)
		AC_DEFINE([BROKEN_CMSG_TYPE], [1],
			[Define if cmsg_type is not passed correctly])
		;;
	esac
	# tun(4) forwarding compat code
	AC_CHECK_HEADERS([linux/if_tun.h])
	if test "x$ac_cv_header_linux_if_tun_h" = "xyes" ; then
		AC_DEFINE([SSH_TUN_LINUX], [1],
		    [Open tunnel devices the Linux tun/tap way])
		AC_DEFINE([SSH_TUN_COMPAT_AF], [1],
		    [Use tunnel device compatibility to OpenBSD])
		AC_DEFINE([SSH_TUN_PREPEND_AF], [1],
		    [Prepend the address family to IP tunnel traffic])
	fi
	AC_CHECK_HEADER([linux/if.h],
	    AC_DEFINE([SYS_RDOMAIN_LINUX], [1],
		[Support routing domains using Linux VRF]), [], [
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.H>
#endif
	    ])
	AC_CHECK_HEADERS([linux/seccomp.h linux/filter.h linux/audit.h], [],
	    [], [#include <linux/types.h>])
	# Obtain MIPS ABI
	case "$host" in
	mips*)
		AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#if _MIPS_SIM != _ABIO32
#error
#endif
			]])],[mips_abi="o32"],[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#if _MIPS_SIM != _ABIN32
#error
#endif
				]])],[mips_abi="n32"],[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#if _MIPS_SIM != _ABI64
#error
#endif
					]])],[mips_abi="n64"],[AC_MSG_ERROR([unknown MIPS ABI])
				])
			])
		])
		;;
	esac
	AC_MSG_CHECKING([for seccomp architecture])
	seccomp_audit_arch=
	case "$host" in
	x86_64-*)
		seccomp_audit_arch=AUDIT_ARCH_X86_64
		;;
	i*86-*)
		seccomp_audit_arch=AUDIT_ARCH_I386
		;;
	arm*-*)
		seccomp_audit_arch=AUDIT_ARCH_ARM
		;;
	aarch64*-*)
		seccomp_audit_arch=AUDIT_ARCH_AARCH64
		;;
	s390x-*)
		seccomp_audit_arch=AUDIT_ARCH_S390X
		;;
	s390-*)
		seccomp_audit_arch=AUDIT_ARCH_S390
		;;
	powerpc64-*)
		seccomp_audit_arch=AUDIT_ARCH_PPC64
		;;
	powerpc64le-*)
		seccomp_audit_arch=AUDIT_ARCH_PPC64LE
		;;
	mips-*)
		seccomp_audit_arch=AUDIT_ARCH_MIPS
		;;
	mipsel-*)
		seccomp_audit_arch=AUDIT_ARCH_MIPSEL
		;;
	mips64-*)
		case "$mips_abi" in
		"n32")
			seccomp_audit_arch=AUDIT_ARCH_MIPS64N32
			;;
		"n64")
			seccomp_audit_arch=AUDIT_ARCH_MIPS64
			;;
		esac
		;;
	mips64el-*)
		case "$mips_abi" in
		"n32")
			seccomp_audit_arch=AUDIT_ARCH_MIPSEL64N32
			;;
		"n64")
			seccomp_audit_arch=AUDIT_ARCH_MIPSEL64
			;;
		esac
		;;
	esac
	if test "x$seccomp_audit_arch" != "x" ; then
		AC_MSG_RESULT(["$seccomp_audit_arch"])
		AC_DEFINE_UNQUOTED([SECCOMP_AUDIT_ARCH], [$seccomp_audit_arch],
		    [Specify the system call convention in use])
	else
		AC_MSG_RESULT([architecture not supported])
	fi
	;;
mips-sony-bsd|mips-sony-newsos4)
	AC_DEFINE([NEED_SETPGRP], [1], [Need setpgrp to acquire controlling tty])
	SONY=1
	;;
*-*-netbsd*)
	check_for_libcrypt_before=1
	if test "x$withval" != "xno" ; then
		need_dash_r=1
	fi
	CPPFLAGS="$CPPFLAGS -D_OPENBSD_SOURCE"
	AC_DEFINE([SSH_TUN_FREEBSD], [1], [Open tunnel devices the FreeBSD way])
	AC_CHECK_HEADER([net/if_tap.h], ,
	    AC_DEFINE([SSH_TUN_NO_L2], [1], [No layer 2 tunnel support]))
	AC_DEFINE([SSH_TUN_PREPEND_AF], [1],
	    [Prepend the address family to IP tunnel traffic])
	TEST_MALLOC_OPTIONS="AJRX"
	AC_DEFINE([BROKEN_READ_COMPARISON], [1],
	    [NetBSD read function is sometimes redirected, breaking atomicio comparisons against it])
	;;
*-*-freebsd*)
	check_for_libcrypt_later=1
	AC_DEFINE([LOCKED_PASSWD_PREFIX], ["*LOCKED*"], [Account locked with pw(1)])
	AC_DEFINE([SSH_TUN_FREEBSD], [1], [Open tunnel devices the FreeBSD way])
	AC_CHECK_HEADER([net/if_tap.h], ,
	    AC_DEFINE([SSH_TUN_NO_L2], [1], [No layer 2 tunnel support]))
	AC_DEFINE([BROKEN_GLOB], [1], [FreeBSD glob does not do what we need])
	TEST_MALLOC_OPTIONS="AJRX"
	# Preauth crypto occasionally uses file descriptors for crypto offload
	# and will crash if they cannot be opened.
	AC_DEFINE([SANDBOX_SKIP_RLIMIT_NOFILE], [1],
	    [define if setrlimit RLIMIT_NOFILE breaks things])
	;;
*-*-bsdi*)
	AC_DEFINE([SETEUID_BREAKS_SETUID])
	AC_DEFINE([BROKEN_SETREUID])
	AC_DEFINE([BROKEN_SETREGID])
	;;
*-next-*)
	conf_lastlog_location="/usr/adm/lastlog"
	conf_utmp_location=/etc/utmp
	conf_wtmp_location=/usr/adm/wtmp
	maildir=/usr/spool/mail
	AC_DEFINE([HAVE_NEXT], [1], [Define if you are on NeXT])
	AC_DEFINE([BROKEN_REALPATH])
	AC_DEFINE([USE_PIPES])
	AC_DEFINE([BROKEN_SAVED_UIDS], [1], [Needed for NeXT])
	;;
*-*-openbsd*)
	use_pie=auto
	AC_DEFINE([HAVE_ATTRIBUTE__SENTINEL__], [1], [OpenBSD's gcc has sentinel])
	AC_DEFINE([HAVE_ATTRIBUTE__BOUNDED__], [1], [OpenBSD's gcc has bounded])
	AC_DEFINE([SSH_TUN_OPENBSD], [1], [Open tunnel devices the OpenBSD way])
	AC_DEFINE([SYSLOG_R_SAFE_IN_SIGHAND], [1],
	    [syslog_r function is safe to use in in a signal handler])
	TEST_MALLOC_OPTIONS="AFGJPRX"
	;;
*-*-solaris*)
	if test "x$withval" != "xno" ; then
		need_dash_r=1
	fi
	AC_DEFINE([PAM_SUN_CODEBASE])
	AC_DEFINE([LOGIN_NEEDS_UTMPX])
	AC_DEFINE([PAM_TTY_KLUDGE])
	AC_DEFINE([SSHPAM_CHAUTHTOK_NEEDS_RUID], [1],
		[Define if pam_chauthtok wants real uid set
		to the unpriv'ed user])
	AC_DEFINE([LOCKED_PASSWD_STRING], ["*LK*"])
	# Pushing STREAMS modules will cause sshd to acquire a controlling tty.
	AC_DEFINE([SSHD_ACQUIRES_CTTY], [1],
		[Define if sshd somehow reacquires a controlling TTY
		after setsid()])
	AC_DEFINE([PASSWD_NEEDS_USERNAME], [1], [must supply username to passwd
		in case the name is longer than 8 chars])
	AC_DEFINE([BROKEN_TCGETATTR_ICANON], [1], [tcgetattr with ICANON may hang])
	external_path_file=/etc/default/login
	# hardwire lastlog location (can't detect it on some versions)
	conf_lastlog_location="/var/adm/lastlog"
	AC_MSG_CHECKING([for obsolete utmp and wtmp in solaris2.x])
	sol2ver=`echo "$host"| sed -e 's/.*[[0-9]]\.//'`
	if test "$sol2ver" -ge 8; then
		AC_MSG_RESULT([yes])
		AC_DEFINE([DISABLE_UTMP])
		AC_DEFINE([DISABLE_WTMP], [1],
			[Define if you don't want to use wtmp])
	else
		AC_MSG_RESULT([no])
	fi
	AC_CHECK_FUNCS([setpflags])
	AC_CHECK_FUNCS([setppriv])
	AC_CHECK_FUNCS([priv_basicset])
	AC_CHECK_HEADERS([priv.h])
	AC_ARG_WITH([solaris-contracts],
		[  --with-solaris-contracts Enable Solaris process contracts (experimental)],
		[
		AC_CHECK_LIB([contract], [ct_tmpl_activate],
			[ AC_DEFINE([USE_SOLARIS_PROCESS_CONTRACTS], [1],
				[Define if you have Solaris process contracts])
			  LIBS="$LIBS -lcontract"
			  SPC_MSG="yes" ], )
		],
	)
	AC_ARG_WITH([solaris-projects],
		[  --with-solaris-projects Enable Solaris projects (experimental)],
		[
		AC_CHECK_LIB([project], [setproject],
			[ AC_DEFINE([USE_SOLARIS_PROJECTS], [1],
				[Define if you have Solaris projects])
			LIBS="$LIBS -lproject"
			SP_MSG="yes" ], )
		],
	)
	AC_ARG_WITH([solaris-privs],
		[  --with-solaris-privs    Enable Solaris/Illumos privileges (experimental)],
		[
		AC_MSG_CHECKING([for Solaris/Illumos privilege support])
		if test "x$ac_cv_func_setppriv" = "xyes" -a \
			"x$ac_cv_header_priv_h" = "xyes" ; then
			SOLARIS_PRIVS=yes
			AC_MSG_RESULT([found])
			AC_DEFINE([NO_UID_RESTORATION_TEST], [1],
				[Define to disable UID restoration test])
			AC_DEFINE([USE_SOLARIS_PRIVS], [1],
				[Define if you have Solaris privileges])
			SPP_MSG="yes"
		else
			AC_MSG_RESULT([not found])
			AC_MSG_ERROR([*** must have support for Solaris privileges to use --with-solaris-privs])
		fi
		],
	)
	TEST_SHELL=$SHELL	# let configure find us a capable shell
	;;
*-*-sunos4*)
	CPPFLAGS="$CPPFLAGS -DSUNOS4"
	AC_CHECK_FUNCS([getpwanam])
	AC_DEFINE([PAM_SUN_CODEBASE])
	conf_utmp_location=/etc/utmp
	conf_wtmp_location=/var/adm/wtmp
	conf_lastlog_location=/var/adm/lastlog
	AC_DEFINE([USE_PIPES])
	AC_DEFINE([DISABLE_UTMPX], [1], [no utmpx])
	;;
*-ncr-sysv*)
	LIBS="$LIBS -lc89"
	AC_DEFINE([USE_PIPES])
	AC_DEFINE([SSHD_ACQUIRES_CTTY])
	AC_DEFINE([SETEUID_BREAKS_SETUID])
	AC_DEFINE([BROKEN_SETREUID])
	AC_DEFINE([BROKEN_SETREGID])
	;;
*-sni-sysv*)
	# /usr/ucblib MUST NOT be searched on ReliantUNIX
	AC_CHECK_LIB([dl], [dlsym], ,)
	# -lresolv needs to be at the end of LIBS or DNS lookups break
	AC_CHECK_LIB([resolv], [res_query], [ LIBS="$LIBS -lresolv" ])
	IPADDR_IN_DISPLAY=yes
	AC_DEFINE([USE_PIPES])
	AC_DEFINE([IP_TOS_IS_BROKEN])
	AC_DEFINE([SETEUID_BREAKS_SETUID])
	AC_DEFINE([BROKEN_SETREUID])
	AC_DEFINE([BROKEN_SETREGID])
	AC_DEFINE([SSHD_ACQUIRES_CTTY])
	external_path_file=/etc/default/login
	# /usr/ucblib/libucb.a no longer needed on ReliantUNIX
	# Attention: always take care to bind libsocket and libnsl before libc,
	# otherwise you will find lots of "SIOCGPGRP errno 22" on syslog
	;;
# UnixWare 1.x, UnixWare 2.x, and others based on code from Univel.
*-*-sysv4.2*)
	AC_DEFINE([USE_PIPES])
	AC_DEFINE([SETEUID_BREAKS_SETUID])
	AC_DEFINE([BROKEN_SETREUID])
	AC_DEFINE([BROKEN_SETREGID])
	AC_DEFINE([PASSWD_NEEDS_USERNAME], [1], [must supply username to passwd])
	AC_DEFINE([LOCKED_PASSWD_STRING], ["*LK*"])
	TEST_SHELL=$SHELL	# let configure find us a capable shell
	;;
# UnixWare 7.x, OpenUNIX 8
*-*-sysv5*)
	CPPFLAGS="$CPPFLAGS -Dvsnprintf=_xvsnprintf -Dsnprintf=_xsnprintf"
	AC_DEFINE([UNIXWARE_LONG_PASSWORDS], [1], [Support passwords > 8 chars])
	AC_DEFINE([USE_PIPES])
	AC_DEFINE([SETEUID_BREAKS_SETUID])
	AC_DEFINE([BROKEN_GETADDRINFO])
	AC_DEFINE([BROKEN_SETREUID])
	AC_DEFINE([BROKEN_SETREGID])
	AC_DEFINE([PASSWD_NEEDS_USERNAME])
	AC_DEFINE([BROKEN_TCGETATTR_ICANON])
	TEST_SHELL=$SHELL	# let configure find us a capable shell
	check_for_libcrypt_later=1
	case "$host" in
	*-*-sysv5SCO_SV*)	# SCO OpenServer 6.x
		maildir=/var/spool/mail
		AC_DEFINE([BROKEN_UPDWTMPX])
		AC_CHECK_LIB([prot], [getluid], [ LIBS="$LIBS -lprot"
			AC_CHECK_FUNCS([getluid setluid], , , [-lprot])
			], , )
		;;
	*)	AC_DEFINE([LOCKED_PASSWD_STRING], ["*LK*"])
		;;
	esac
	;;
*-*-sysv*)
	;;
# SCO UNIX and OEM versions of SCO UNIX
*-*-sco3.2v4*)
	AC_MSG_ERROR("This Platform is no longer supported.")
	;;
# SCO OpenServer 5.x
*-*-sco3.2v5*)
	if test -z "$GCC"; then
		CFLAGS="$CFLAGS -belf"
	fi
	LIBS="$LIBS -lprot -lx -ltinfo -lm"
	no_dev_ptmx=1
	AC_DEFINE([USE_PIPES])
	AC_DEFINE([HAVE_SECUREWARE])
	AC_DEFINE([DISABLE_SHADOW])
	AC_DEFINE([DISABLE_FD_PASSING])
	AC_DEFINE([SETEUID_BREAKS_SETUID])
	AC_DEFINE([BROKEN_GETADDRINFO])
	AC_DEFINE([BROKEN_SETREUID])
	AC_DEFINE([BROKEN_SETREGID])
	AC_DEFINE([WITH_ABBREV_NO_TTY])
	AC_DEFINE([BROKEN_UPDWTMPX])
	AC_DEFINE([PASSWD_NEEDS_USERNAME])
	AC_CHECK_FUNCS([getluid setluid])
	MANTYPE=man
	TEST_SHELL=$SHELL	# let configure find us a capable shell
	SKIP_DISABLE_LASTLOG_DEFINE=yes
	;;
*-dec-osf*)
	AC_MSG_CHECKING([for Digital Unix SIA])
	no_osfsia=""
	AC_ARG_WITH([osfsia],
		[  --with-osfsia           Enable Digital Unix SIA],
		[
			if test "x$withval" = "xno" ; then
				AC_MSG_RESULT([disabled])
				no_osfsia=1
			fi
		],
	)
	if test -z "$no_osfsia" ; then
		if test -f /etc/sia/matrix.conf; then
			AC_MSG_RESULT([yes])
			AC_DEFINE([HAVE_OSF_SIA], [1],
				[Define if you have Digital Unix Security
				Integration Architecture])
			AC_DEFINE([DISABLE_LOGIN], [1],
				[Define if you don't want to use your
				system's login() call])
			AC_DEFINE([DISABLE_FD_PASSING])
			LIBS="$LIBS -lsecurity -ldb -lm -laud"
			SIA_MSG="yes"
		else
			AC_MSG_RESULT([no])
			AC_DEFINE([LOCKED_PASSWD_SUBSTR], ["Nologin"],
			  [String used in /etc/passwd to denote locked account])
		fi
	fi
	AC_DEFINE([BROKEN_GETADDRINFO])
	AC_DEFINE([SETEUID_BREAKS_SETUID])
	AC_DEFINE([BROKEN_SETREUID])
	AC_DEFINE([BROKEN_SETREGID])
	AC_DEFINE([BROKEN_READV_COMPARISON], [1], [Can't do comparisons on readv])
	;;

*-*-nto-qnx*)
	AC_DEFINE([USE_PIPES])
	AC_DEFINE([NO_X11_UNIX_SOCKETS])
	AC_DEFINE([DISABLE_LASTLOG])
	AC_DEFINE([SSHD_ACQUIRES_CTTY])
	AC_DEFINE([BROKEN_SHADOW_EXPIRE], [1], [QNX shadow support is broken])
	enable_etc_default_login=no	# has incompatible /etc/default/login
	case "$host" in
	*-*-nto-qnx6*)
		AC_DEFINE([DISABLE_FD_PASSING])
		;;
	esac
	;;

*-*-ultrix*)
	AC_DEFINE([BROKEN_GETGROUPS], [1], [getgroups(0,NULL) will return -1])
	AC_DEFINE([NEED_SETPGRP])
	AC_DEFINE([HAVE_SYS_SYSLOG_H], [1], [Force use of sys/syslog.h on Ultrix])
	;;

*-*-lynxos)
	CFLAGS="$CFLAGS -D__NO_INCLUDE_WARN__"
	AC_DEFINE([BROKEN_SETVBUF], [1],
	    [LynxOS has broken setvbuf() implementation])
	;;
esac

AC_MSG_CHECKING([compiler and flags for sanity])
AC_RUN_IFELSE([AC_LANG_PROGRAM([[ #include <stdio.h> ]], [[ exit(0); ]])],
	[	AC_MSG_RESULT([yes]) ],
	[
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([*** compiler cannot create working executables, check config.log ***])
	],
	[	AC_MSG_WARN([cross compiling: not checking compiler sanity]) ]
)

dnl Checks for header files.
# Checks for libraries.
AC_CHECK_FUNC([setsockopt], , [AC_CHECK_LIB([socket], [setsockopt])])

dnl IRIX and Solaris 2.5.1 have dirname() in libgen
AC_CHECK_FUNCS([dirname], [AC_CHECK_HEADERS([libgen.h])] , [
	AC_CHECK_LIB([gen], [dirname], [
		AC_CACHE_CHECK([for broken dirname],
			ac_cv_have_broken_dirname, [
			save_LIBS="$LIBS"
			LIBS="$LIBS -lgen"
			AC_RUN_IFELSE(
				[AC_LANG_SOURCE([[
#include <libgen.h>
#include <string.h>

int main(int argc, char **argv) {
    char *s, buf[32];

    strncpy(buf,"/etc", 32);
    s = dirname(buf);
    if (!s || strncmp(s, "/", 32) != 0) {
	exit(1);
    } else {
	exit(0);
    }
}
				]])],
				[ ac_cv_have_broken_dirname="no" ],
				[ ac_cv_have_broken_dirname="yes" ],
				[ ac_cv_have_broken_dirname="no" ],
			)
			LIBS="$save_LIBS"
		])
		if test "x$ac_cv_have_broken_dirname" = "xno" ; then
			LIBS="$LIBS -lgen"
			AC_DEFINE([HAVE_DIRNAME])
			AC_CHECK_HEADERS([libgen.h])
		fi
	])
])

AC_CHECK_FUNC([getspnam], ,
	[AC_CHECK_LIB([gen], [getspnam], [LIBS="$LIBS -lgen"])])
AC_SEARCH_LIBS([basename], [gen], [AC_DEFINE([HAVE_BASENAME], [1],
	[Define if you have the basename function.])])

dnl zlib is required
AC_ARG_WITH([zlib],
	[  --with-zlib=PATH        Use zlib in PATH],
	[ if test "x$withval" = "xno" ; then
		AC_MSG_ERROR([*** zlib is required ***])
	  elif test "x$withval" != "xyes"; then
		if test -d "$withval/lib"; then
			if test -n "${need_dash_r}"; then
				LDFLAGS="-L${withval}/lib -R${withval}/lib ${LDFLAGS}"
			else
				LDFLAGS="-L${withval}/lib ${LDFLAGS}"
			fi
		else
			if test -n "${need_dash_r}"; then
				LDFLAGS="-L${withval} -R${withval} ${LDFLAGS}"
			else
				LDFLAGS="-L${withval} ${LDFLAGS}"
			fi
		fi
		if test -d "$withval/include"; then
			CPPFLAGS="-I${withval}/include ${CPPFLAGS}"
		else
			CPPFLAGS="-I${withval} ${CPPFLAGS}"
		fi
	fi ]
)

AC_CHECK_HEADER([zlib.h], ,[AC_MSG_ERROR([*** zlib.h missing - please install first or check config.log ***])])
AC_CHECK_LIB([z], [deflate], ,
	[
		saved_CPPFLAGS="$CPPFLAGS"
		saved_LDFLAGS="$LDFLAGS"
		save_LIBS="$LIBS"
		dnl Check default zlib install dir
		if test -n "${need_dash_r}"; then
			LDFLAGS="-L/usr/local/lib -R/usr/local/lib ${saved_LDFLAGS}"
		else
			LDFLAGS="-L/usr/local/lib ${saved_LDFLAGS}"
		fi
		CPPFLAGS="-I/usr/local/include ${saved_CPPFLAGS}"
		LIBS="$LIBS -lz"
		AC_TRY_LINK_FUNC([deflate], [AC_DEFINE([HAVE_LIBZ])],
			[
				AC_MSG_ERROR([*** zlib missing - please install first or check config.log ***])
			]
		)
	]
)

AC_ARG_WITH([zlib-version-check],
	[  --without-zlib-version-check Disable zlib version check],
	[  if test "x$withval" = "xno" ; then
		zlib_check_nonfatal=1
	   fi
	]
)

AC_MSG_CHECKING([for possibly buggy zlib])
AC_RUN_IFELSE([AC_LANG_PROGRAM([[
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
	]],
	[[
	int a=0, b=0, c=0, d=0, n, v;
	n = sscanf(ZLIB_VERSION, "%d.%d.%d.%d", &a, &b, &c, &d);
	if (n != 3 && n != 4)
		exit(1);
	v = a*1000000 + b*10000 + c*100 + d;
	fprintf(stderr, "found zlib version %s (%d)\n", ZLIB_VERSION, v);

	/* 1.1.4 is OK */
	if (a == 1 && b == 1 && c >= 4)
		exit(0);

	/* 1.2.3 and up are OK */
	if (v >= 1020300)
		exit(0);

	exit(2);
	]])],
	AC_MSG_RESULT([no]),
	[ AC_MSG_RESULT([yes])
	  if test -z "$zlib_check_nonfatal" ; then
		AC_MSG_ERROR([*** zlib too old - check config.log ***
Your reported zlib version has known security problems.  It's possible your
vendor has fixed these problems without changing the version number.  If you
are sure this is the case, you can disable the check by running
"./configure --without-zlib-version-check".
If you are in doubt, upgrade zlib to version 1.2.3 or greater.
See http://www.gzip.org/zlib/ for details.])
	  else
		AC_MSG_WARN([zlib version may have security problems])
	  fi
	],
	[	AC_MSG_WARN([cross compiling: not checking zlib version]) ]
)

dnl UnixWare 2.x
AC_CHECK_FUNC([strcasecmp],
	[], [ AC_CHECK_LIB([resolv], [strcasecmp], [LIBS="$LIBS -lresolv"]) ]
)
AC_CHECK_FUNCS([utimes],
	[], [ AC_CHECK_LIB([c89], [utimes], [AC_DEFINE([HAVE_UTIMES])
					LIBS="$LIBS -lc89"]) ]
)

dnl    Checks for libutil functions
AC_CHECK_HEADERS([bsd/libutil.h libutil.h])
AC_SEARCH_LIBS([fmt_scaled], [util bsd])
AC_SEARCH_LIBS([scan_scaled], [util bsd])
AC_SEARCH_LIBS([login], [util bsd])
AC_SEARCH_LIBS([logout], [util bsd])
AC_SEARCH_LIBS([logwtmp], [util bsd])
AC_SEARCH_LIBS([openpty], [util bsd])
AC_SEARCH_LIBS([updwtmp], [util bsd])
AC_CHECK_FUNCS([fmt_scaled scan_scaled login logout openpty updwtmp logwtmp])

# On some platforms, inet_ntop and gethostbyname may be found in libresolv
# or libnsl.
AC_SEARCH_LIBS([inet_ntop], [resolv nsl])
AC_SEARCH_LIBS([gethostbyname], [resolv nsl])

# "Particular Function Checks"
# see https://www.gnu.org/software/autoconf/manual/autoconf-2.69/html_node/Particular-Functions.html
AC_FUNC_STRFTIME
AC_FUNC_MALLOC
AC_FUNC_REALLOC
# autoconf doesn't have AC_FUNC_CALLOC so fake it if malloc returns NULL;
AC_MSG_CHECKING([if calloc(0, N) returns non-null])
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM(
		[[ #include <stdlib.h> ]],
		[[ void *p = calloc(0, 1); exit(p == NULL); ]]
	)],
	[ func_calloc_0_nonnull=yes ],
	[ func_calloc_0_nonnull=no ],
	[ AC_MSG_WARN([cross compiling: assuming same as malloc])
	  func_calloc_0_nonnull="$ac_cv_func_malloc_0_nonnull"]
)
AC_MSG_RESULT([$func_calloc_0_nonnull])

if test "x$func_calloc_0_nonnull" = "xyes"; then
	AC_DEFINE(HAVE_CALLOC, 1, [calloc(0, x) returns non-null])
else
	AC_DEFINE(HAVE_CALLOC, 0, [calloc(0, x) returns NULL])
	AC_DEFINE(calloc, rpl_calloc,
	    [Define to rpl_calloc if the replacement function should be used.])
fi

# Check for ALTDIRFUNC glob() extension
AC_MSG_CHECKING([for GLOB_ALTDIRFUNC support])
AC_EGREP_CPP([FOUNDIT],
	[
		#include <glob.h>
		#ifdef GLOB_ALTDIRFUNC
		FOUNDIT
		#endif
	],
	[
		AC_DEFINE([GLOB_HAS_ALTDIRFUNC], [1],
			[Define if your system glob() function has
			the GLOB_ALTDIRFUNC extension])
		AC_MSG_RESULT([yes])
	],
	[
		AC_MSG_RESULT([no])
	]
)

# Check for g.gl_matchc glob() extension
AC_MSG_CHECKING([for gl_matchc field in glob_t])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <glob.h> ]],
	[[ glob_t g; g.gl_matchc = 1; ]])],
	[
		AC_DEFINE([GLOB_HAS_GL_MATCHC], [1],
			[Define if your system glob() function has
			gl_matchc options in glob_t])
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])
])

# Check for g.gl_statv glob() extension
AC_MSG_CHECKING([for gl_statv and GLOB_KEEPSTAT extensions for glob])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <glob.h> ]], [[
#ifndef GLOB_KEEPSTAT
#error "glob does not support GLOB_KEEPSTAT extension"
#endif
glob_t g;
g.gl_statv = NULL;
]])],
	[
		AC_DEFINE([GLOB_HAS_GL_STATV], [1],
			[Define if your system glob() function has
			gl_statv options in glob_t])
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])

])

AC_CHECK_DECLS([GLOB_NOMATCH], , , [#include <glob.h>])

AC_CHECK_DECL([VIS_ALL], ,
    AC_DEFINE(BROKEN_STRNVIS, 1, [missing VIS_ALL]), [#include <vis.h>])

AC_MSG_CHECKING([whether struct dirent allocates space for d_name])
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <dirent.h>]],
	[[
	struct dirent d;
	exit(sizeof(d.d_name)<=sizeof(char));
	]])],
	[AC_MSG_RESULT([yes])],
	[
		AC_MSG_RESULT([no])
		AC_DEFINE([BROKEN_ONE_BYTE_DIRENT_D_NAME], [1],
			[Define if your struct dirent expects you to
			allocate extra space for d_name])
	],
	[
		AC_MSG_WARN([cross compiling: assuming BROKEN_ONE_BYTE_DIRENT_D_NAME])
		AC_DEFINE([BROKEN_ONE_BYTE_DIRENT_D_NAME])
	]
)

AC_MSG_CHECKING([for /proc/pid/fd directory])
if test -d "/proc/$$/fd" ; then
	AC_DEFINE([HAVE_PROC_PID], [1], [Define if you have /proc/$pid/fd])
	AC_MSG_RESULT([yes])
else
	AC_MSG_RESULT([no])
fi

# Check whether user wants TCP wrappers support
TCPW_MSG="no"
AC_ARG_WITH([tcp-wrappers],
	[  --with-tcp-wrappers[[=PATH]] Enable tcpwrappers support (optionally in PATH)],
	[
		if test "x$withval" != "xno" ; then
			saved_LIBS="$LIBS"
			saved_LDFLAGS="$LDFLAGS"
			saved_CPPFLAGS="$CPPFLAGS"
			if test -n "${withval}" && \
			    test "x${withval}" != "xyes"; then
				if test -d "${withval}/lib"; then
					if test -n "${need_dash_r}"; then
						LDFLAGS="-L${withval}/lib -R${withval}/lib ${LDFLAGS}"
					else
						LDFLAGS="-L${withval}/lib ${LDFLAGS}"
					fi
				else
					if test -n "${need_dash_r}"; then
						LDFLAGS="-L${withval} -R${withval} ${LDFLAGS}"
					else
						LDFLAGS="-L${withval} ${LDFLAGS}"
					fi
				fi
				if test -d "${withval}/include"; then
					CPPFLAGS="-I${withval}/include ${CPPFLAGS}"
				else
					CPPFLAGS="-I${withval} ${CPPFLAGS}"
				fi
			fi
			LIBS="-lwrap $LIBS"
			AC_MSG_CHECKING([for libwrap])
			AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <tcpd.h>
int deny_severity = 0, allow_severity = 0;
				]], [[
	hosts_access(0);
				]])], [
					AC_MSG_RESULT([yes])
					AC_DEFINE([LIBWRAP], [1],
						[Define if you want
						TCP Wrappers support])
					SSHDLIBS="$SSHDLIBS -lwrap"
					TCPW_MSG="yes"
				], [
					AC_MSG_ERROR([*** libwrap missing])
			])
			LIBS="$saved_LIBS"
		fi
	]
)

# Check whether user wants to use ldns
LDNS_MSG="no"
AC_ARG_WITH(ldns,
	[  --with-ldns[[=PATH]]      Use ldns for DNSSEC support (optionally in PATH)],
	[
	ldns=""
	if test "x$withval" = "xyes" ; then
		AC_PATH_TOOL([LDNSCONFIG], [ldns-config], [no])
		if test "x$LDNSCONFIG" = "xno"; then
			CPPFLAGS="$CPPFLAGS -I${withval}/include"
			LDFLAGS="$LDFLAGS -L${withval}/lib"
			LIBS="-lldns $LIBS"
			ldns=yes
		else
			LIBS="$LIBS `$LDNSCONFIG --libs`"
			CPPFLAGS="$CPPFLAGS `$LDNSCONFIG --cflags`"
			ldns=yes
		fi
	elif test "x$withval" != "xno" ; then
			CPPFLAGS="$CPPFLAGS -I${withval}/include"
			LDFLAGS="$LDFLAGS -L${withval}/lib"
			LIBS="-lldns $LIBS"
			ldns=yes
	fi

	# Verify that it works.
	if test "x$ldns" = "xyes" ; then
		AC_DEFINE(HAVE_LDNS, 1, [Define if you want ldns support])
		LDNS_MSG="yes"
		AC_MSG_CHECKING([for ldns support])
		AC_LINK_IFELSE(
			[AC_LANG_SOURCE([[
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ldns/ldns.h>
int main() { ldns_status status = ldns_verify_trusted(NULL, NULL, NULL, NULL); status=LDNS_STATUS_OK; exit(0); }
			]])
		],
			[AC_MSG_RESULT(yes)],
				[
					AC_MSG_RESULT(no)
					AC_MSG_ERROR([** Incomplete or missing ldns libraries.])
				])
	fi
])

# Check whether user wants libedit support
LIBEDIT_MSG="no"
AC_ARG_WITH([libedit],
	[  --with-libedit[[=PATH]]   Enable libedit support for sftp],
	[ if test "x$withval" != "xno" ; then
		if test "x$withval" = "xyes" ; then
			AC_PATH_TOOL([PKGCONFIG], [pkg-config], [no])
			if test "x$PKGCONFIG" != "xno"; then
				AC_MSG_CHECKING([if $PKGCONFIG knows about libedit])
				if "$PKGCONFIG" libedit; then
					AC_MSG_RESULT([yes])
					use_pkgconfig_for_libedit=yes
				else
					AC_MSG_RESULT([no])
				fi
			fi
		else
			CPPFLAGS="$CPPFLAGS -I${withval}/include"
			if test -n "${need_dash_r}"; then
				LDFLAGS="-L${withval}/lib -R${withval}/lib ${LDFLAGS}"
			else
				LDFLAGS="-L${withval}/lib ${LDFLAGS}"
			fi
		fi
		if test "x$use_pkgconfig_for_libedit" = "xyes"; then
			LIBEDIT=`$PKGCONFIG --libs libedit`
			CPPFLAGS="$CPPFLAGS `$PKGCONFIG --cflags libedit`"
		else
			LIBEDIT="-ledit -lcurses"
		fi
		OTHERLIBS=`echo $LIBEDIT | sed 's/-ledit//'`
		AC_CHECK_LIB([edit], [el_init],
			[ AC_DEFINE([USE_LIBEDIT], [1], [Use libedit for sftp])
			  LIBEDIT_MSG="yes"
			  AC_SUBST([LIBEDIT])
			],
			[ AC_MSG_ERROR([libedit not found]) ],
			[ $OTHERLIBS ]
		)
		AC_MSG_CHECKING([if libedit version is compatible])
		AC_COMPILE_IFELSE(
		    [AC_LANG_PROGRAM([[ #include <histedit.h> ]],
		    [[
	int i = H_SETSIZE;
	el_init("", NULL, NULL, NULL);
	exit(0);
		    ]])],
		    [ AC_MSG_RESULT([yes]) ],
		    [ AC_MSG_RESULT([no])
		      AC_MSG_ERROR([libedit version is not compatible]) ]
		)
	fi ]
)

AUDIT_MODULE=none
AC_ARG_WITH([audit],
	[  --with-audit=module     Enable audit support (modules=debug,bsm,linux)],
	[
	  AC_MSG_CHECKING([for supported audit module])
	  case "$withval" in
	  bsm)
		AC_MSG_RESULT([bsm])
		AUDIT_MODULE=bsm
		dnl    Checks for headers, libs and functions
		AC_CHECK_HEADERS([bsm/audit.h], [],
		    [AC_MSG_ERROR([BSM enabled and bsm/audit.h not found])],
		    [
#ifdef HAVE_TIME_H
# include <time.h>
#endif
		    ]
)
		AC_CHECK_LIB([bsm], [getaudit], [],
		    [AC_MSG_ERROR([BSM enabled and required library not found])])
		AC_CHECK_FUNCS([getaudit], [],
		    [AC_MSG_ERROR([BSM enabled and required function not found])])
		# These are optional
		AC_CHECK_FUNCS([getaudit_addr aug_get_machine])
		AC_DEFINE([USE_BSM_AUDIT], [1], [Use BSM audit module])
		if test "$sol2ver" -ge 11; then
			SSHDLIBS="$SSHDLIBS -lscf"
			AC_DEFINE([BROKEN_BSM_API], [1],
				[The system has incomplete BSM API])
		fi
		;;
	  linux)
		AC_MSG_RESULT([linux])
		AUDIT_MODULE=linux
		dnl    Checks for headers, libs and functions
		AC_CHECK_HEADERS([libaudit.h])
		SSHDLIBS="$SSHDLIBS -laudit"
		AC_DEFINE([USE_LINUX_AUDIT], [1], [Use Linux audit module])
		;;
	  debug)
		AUDIT_MODULE=debug
		AC_MSG_RESULT([debug])
		AC_DEFINE([SSH_AUDIT_EVENTS], [1], [Use audit debugging module])
		;;
	  no)
		AC_MSG_RESULT([no])
		;;
	  *)
		AC_MSG_ERROR([Unknown audit module $withval])
		;;
	esac ]
)

AC_ARG_WITH([pie],
    [  --with-pie              Build Position Independent Executables if possible], [
	if test "x$withval" = "xno"; then
		use_pie=no
	fi
	if test "x$withval" = "xyes"; then
		use_pie=yes
	fi
    ]
)
if test "x$use_pie" = "x"; then
	use_pie=no
fi
if test "x$use_toolchain_hardening" != "x1" && test "x$use_pie" = "xauto"; then
	# Turn off automatic PIE when toolchain hardening is off.
	use_pie=no
fi
if test "x$use_pie" = "xauto"; then
	# Automatic PIE requires gcc >= 4.x
	AC_MSG_CHECKING([for gcc >= 4.x])
	AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#if !defined(__GNUC__) || __GNUC__ < 4
#error gcc is too old
#endif
]])],
	[ AC_MSG_RESULT([yes]) ],
	[ AC_MSG_RESULT([no])
	  use_pie=no ]
)
fi
if test "x$use_pie" != "xno"; then
	SAVED_CFLAGS="$CFLAGS"
	SAVED_LDFLAGS="$LDFLAGS"
	OSSH_CHECK_CFLAG_COMPILE([-fPIE])
	OSSH_CHECK_LDFLAG_LINK([-pie])
	# We use both -fPIE and -pie or neither.
	AC_MSG_CHECKING([whether both -fPIE and -pie are supported])
	if echo "x $CFLAGS"  | grep ' -fPIE' >/dev/null 2>&1 && \
	   echo "x $LDFLAGS" | grep ' -pie'  >/dev/null 2>&1 ; then
		AC_MSG_RESULT([yes])
	else
		AC_MSG_RESULT([no])
		CFLAGS="$SAVED_CFLAGS"
		LDFLAGS="$SAVED_LDFLAGS"
	fi
fi

dnl    Checks for library functions. Please keep in alphabetical order
AC_CHECK_FUNCS([ \
	Blowfish_initstate \
	Blowfish_expandstate \
	Blowfish_expand0state \
	Blowfish_stream2word \
	asprintf \
	b64_ntop \
	__b64_ntop \
	b64_pton \
	__b64_pton \
	bcopy \
	bcrypt_pbkdf \
	bindresvport_sa \
	blf_enc \
	bzero \
	cap_rights_limit \
	clock \
	closefrom \
	dirfd \
	endgrent \
	err \
	errx \
	explicit_bzero \
	fchmod \
	fchown \
	flock \
	freeaddrinfo \
	freezero \
	fstatfs \
	fstatvfs \
	futimes \
	getaddrinfo \
	getcwd \
	getgrouplist \
	getline \
	getnameinfo \
	getopt \
	getpagesize \
	getpeereid \
	getpeerucred \
	getpgid \
	_getpty \
	getrlimit \
	getrandom \
	getsid \
	getttyent \
	glob \
	group_from_gid \
	inet_aton \
	inet_ntoa \
	inet_ntop \
	innetgr \
	llabs \
	login_getcapbool \
	md5_crypt \
	memmove \
	memset_s \
	mkdtemp \
	ngetaddrinfo \
	nsleep \
	ogetaddrinfo \
	openlog_r \
	pledge \
	poll \
	prctl \
	pstat \
	raise \
	readpassphrase \
	reallocarray \
	recvmsg \
	recallocarray \
	rresvport_af \
	sendmsg \
	setdtablesize \
	setegid \
	setenv \
	seteuid \
	setgroupent \
	setgroups \
	setlinebuf \
	setlogin \
	setpassent\
	setpcred \
	setproctitle \
	setregid \
	setreuid \
	setrlimit \
	setsid \
	setvbuf \
	sigaction \
	sigvec \
	snprintf \
	socketpair \
	statfs \
	statvfs \
	strcasestr \
	strdup \
	strerror \
	strlcat \
	strlcpy \
	strmode \
	strndup \
	strnlen \
	strnvis \
	strptime \
	strsignal \
	strtonum \
	strtoll \
	strtoul \
	strtoull \
	swap32 \
	sysconf \
	tcgetpgrp \
	timingsafe_bcmp \
	truncate \
	unsetenv \
	updwtmpx \
	user_from_uid \
	usleep \
	vasprintf \
	vsnprintf \
	waitpid \
	warn \
])

AC_CHECK_DECLS([bzero])

dnl Wide character support.
AC_CHECK_FUNCS([mblen mbtowc nl_langinfo wcwidth])

TEST_SSH_UTF8=${TEST_SSH_UTF8:=yes}
AC_MSG_CHECKING([for utf8 locale support])
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([[
#include <locale.h>
#include <stdlib.h>
	]], [[
	char *loc = setlocale(LC_CTYPE, "en_US.UTF-8");
	if (loc != NULL)
		exit(0);
	exit(1);
	]])],
	AC_MSG_RESULT(yes),
	[AC_MSG_RESULT(no)
	 TEST_SSH_UTF8=no],
	AC_MSG_WARN([cross compiling: assuming yes])
)

AC_LINK_IFELSE(
        [AC_LANG_PROGRAM(
           [[ #include <ctype.h> ]],
           [[ return (isblank('a')); ]])],
	[AC_DEFINE([HAVE_ISBLANK], [1], [Define if you have isblank(3C).])
])

disable_pkcs11=
AC_ARG_ENABLE([pkcs11],
	[  --disable-pkcs11        disable PKCS#11 support code [no]],
	[
		if test "x$enableval" = "xno" ; then
			disable_pkcs11=1
		fi
	]
)

# PKCS11 depends on OpenSSL.
if test "x$openssl" = "xyes" && test "x$disable_pkcs11" = "x"; then
	# PKCS#11 support requires dlopen() and co
	AC_SEARCH_LIBS([dlopen], [dl],
	    AC_CHECK_DECL([RTLD_NOW],
		AC_DEFINE([ENABLE_PKCS11], [], [Enable for PKCS#11 support]),
		[], [#include <dlfcn.h>]
	    )
	)
fi

# IRIX has a const char return value for gai_strerror()
AC_CHECK_FUNCS([gai_strerror], [
	AC_DEFINE([HAVE_GAI_STRERROR])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

const char *gai_strerror(int);
			]], [[
	char *str;
	str = gai_strerror(0);
			]])], [
		AC_DEFINE([HAVE_CONST_GAI_STRERROR_PROTO], [1],
		[Define if gai_strerror() returns const char *])], [])])

AC_SEARCH_LIBS([nanosleep], [rt posix4], [AC_DEFINE([HAVE_NANOSLEEP], [1],
	[Some systems put nanosleep outside of libc])])

AC_SEARCH_LIBS([clock_gettime], [rt],
	[AC_DEFINE([HAVE_CLOCK_GETTIME], [1], [Have clock_gettime])])

dnl Make sure prototypes are defined for these before using them.
AC_CHECK_DECL([strsep],
	[AC_CHECK_FUNCS([strsep])],
	[],
	[
#ifdef HAVE_STRING_H
# include <string.h>
#endif
	])

dnl tcsendbreak might be a macro
AC_CHECK_DECL([tcsendbreak],
	[AC_DEFINE([HAVE_TCSENDBREAK])],
	[AC_CHECK_FUNCS([tcsendbreak])],
	[#include <termios.h>]
)

AC_CHECK_DECLS([h_errno], , ,[#include <netdb.h>])

AC_CHECK_DECLS([SHUT_RD], , ,
	[
#include <sys/types.h>
#include <sys/socket.h>
	])

AC_CHECK_DECLS([O_NONBLOCK], , ,
	[
#include <sys/types.h>
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
	])

AC_CHECK_DECLS([readv, writev], , , [
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
	])

AC_CHECK_DECLS([MAXSYMLINKS], , , [
#include <sys/param.h>
	])

AC_CHECK_DECLS([offsetof], , , [
#include <stddef.h>
	])

# extra bits for select(2)
AC_CHECK_DECLS([howmany, NFDBITS], [], [], [[
#include <sys/param.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
	]])
AC_CHECK_TYPES([fd_mask], [], [], [[
#include <sys/param.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
	]])

AC_CHECK_FUNCS([setresuid], [
	dnl Some platorms have setresuid that isn't implemented, test for this
	AC_MSG_CHECKING([if setresuid seems to work])
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[
#include <stdlib.h>
#include <errno.h>
		]], [[
	errno=0;
	setresuid(0,0,0);
	if (errno==ENOSYS)
		exit(1);
	else
		exit(0);
		]])],
		[AC_MSG_RESULT([yes])],
		[AC_DEFINE([BROKEN_SETRESUID], [1],
			[Define if your setresuid() is broken])
		 AC_MSG_RESULT([not implemented])],
		[AC_MSG_WARN([cross compiling: not checking setresuid])]
	)
])

AC_CHECK_FUNCS([setresgid], [
	dnl Some platorms have setresgid that isn't implemented, test for this
	AC_MSG_CHECKING([if setresgid seems to work])
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[
#include <stdlib.h>
#include <errno.h>
		]], [[
	errno=0;
	setresgid(0,0,0);
	if (errno==ENOSYS)
		exit(1);
	else
		exit(0);
		]])],
		[AC_MSG_RESULT([yes])],
		[AC_DEFINE([BROKEN_SETRESGID], [1],
			[Define if your setresgid() is broken])
		 AC_MSG_RESULT([not implemented])],
		[AC_MSG_WARN([cross compiling: not checking setresuid])]
	)
])

AC_CHECK_FUNCS([realpath], [
	dnl the sftp v3 spec says SSH_FXP_REALPATH will "canonicalize any given
	dnl path name", however some implementations of realpath (and some
	dnl versions of the POSIX spec) do not work on non-existent files,
	dnl so we use the OpenBSD implementation on those platforms.
	AC_MSG_CHECKING([if realpath works with non-existent files])
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[
#include <limits.h>
#include <stdlib.h>
#include <errno.h>
		]], [[
		char buf[PATH_MAX];
		if (realpath("/opensshnonexistentfilename1234", buf) == NULL)
			if (errno == ENOENT)
				exit(1);
		exit(0);
		]])],
		[AC_MSG_RESULT([yes])],
		[AC_DEFINE([BROKEN_REALPATH], [1],
			[realpath does not work with nonexistent files])
		 AC_MSG_RESULT([no])],
		[AC_MSG_WARN([cross compiling: assuming working])]
	)
])

AC_MSG_CHECKING([for working fflush(NULL)])
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([[#include <stdio.h>]], [[fflush(NULL); exit(0);]])],
	AC_MSG_RESULT([yes]),
	[AC_MSG_RESULT([no])
	 AC_DEFINE([FFLUSH_NULL_BUG], [1],
	    [define if fflush(NULL) does not work])],
	AC_MSG_WARN([cross compiling: assuming working])
)

dnl    Checks for time functions
AC_CHECK_FUNCS([gettimeofday time])
dnl    Checks for utmp functions
AC_CHECK_FUNCS([endutent getutent getutid getutline pututline setutent])
AC_CHECK_FUNCS([utmpname])
dnl    Checks for utmpx functions
AC_CHECK_FUNCS([endutxent getutxent getutxid getutxline getutxuser pututxline])
AC_CHECK_FUNCS([setutxdb setutxent utmpxname])
dnl    Checks for lastlog functions
AC_CHECK_FUNCS([getlastlogxbyname])

AC_CHECK_FUNC([daemon],
	[AC_DEFINE([HAVE_DAEMON], [1], [Define if your libraries define daemon()])],
	[AC_CHECK_LIB([bsd], [daemon],
		[LIBS="$LIBS -lbsd"; AC_DEFINE([HAVE_DAEMON])])]
)

AC_CHECK_FUNC([getpagesize],
	[AC_DEFINE([HAVE_GETPAGESIZE], [1],
		[Define if your libraries define getpagesize()])],
	[AC_CHECK_LIB([ucb], [getpagesize],
		[LIBS="$LIBS -lucb"; AC_DEFINE([HAVE_GETPAGESIZE])])]
)

# Check for broken snprintf
if test "x$ac_cv_func_snprintf" = "xyes" ; then
	AC_MSG_CHECKING([whether snprintf correctly terminates long strings])
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[ #include <stdio.h> ]],
		[[
	char b[5];
	snprintf(b,5,"123456789");
	exit(b[4]!='\0');
		]])],
		[AC_MSG_RESULT([yes])],
		[
			AC_MSG_RESULT([no])
			AC_DEFINE([BROKEN_SNPRINTF], [1],
				[Define if your snprintf is busted])
			AC_MSG_WARN([****** Your snprintf() function is broken, complain to your vendor])
		],
		[ AC_MSG_WARN([cross compiling: Assuming working snprintf()]) ]
	)
fi

# We depend on vsnprintf returning the right thing on overflow: the
# number of characters it tried to create (as per SUSv3)
if test "x$ac_cv_func_vsnprintf" = "xyes" ; then
	AC_MSG_CHECKING([whether vsnprintf returns correct values on overflow])
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <stdio.h>
#include <stdarg.h>

int x_snprintf(char *str, size_t count, const char *fmt, ...)
{
	size_t ret;
	va_list ap;

	va_start(ap, fmt);
	ret = vsnprintf(str, count, fmt, ap);
	va_end(ap);
	return ret;
}
		]], [[
char x[1];
if (x_snprintf(x, 1, "%s %d", "hello", 12345) != 11)
	return 1;
if (x_snprintf(NULL, 0, "%s %d", "hello", 12345) != 11)
	return 1;
return 0;
		]])],
		[AC_MSG_RESULT([yes])],
		[
			AC_MSG_RESULT([no])
			AC_DEFINE([BROKEN_SNPRINTF], [1],
				[Define if your snprintf is busted])
			AC_MSG_WARN([****** Your vsnprintf() function is broken, complain to your vendor])
		],
		[ AC_MSG_WARN([cross compiling: Assuming working vsnprintf()]) ]
	)
fi

# On systems where [v]snprintf is broken, but is declared in stdio,
# check that the fmt argument is const char * or just char *.
# This is only useful for when BROKEN_SNPRINTF
AC_MSG_CHECKING([whether snprintf can declare const char *fmt])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <stdio.h>
int snprintf(char *a, size_t b, const char *c, ...) { return 0; }
		]], [[
	snprintf(0, 0, 0);
		]])],
   [AC_MSG_RESULT([yes])
    AC_DEFINE([SNPRINTF_CONST], [const],
              [Define as const if snprintf() can declare const char *fmt])],
   [AC_MSG_RESULT([no])
    AC_DEFINE([SNPRINTF_CONST], [/* not const */])])

# Check for missing getpeereid (or equiv) support
NO_PEERCHECK=""
if test "x$ac_cv_func_getpeereid" != "xyes" -a "x$ac_cv_func_getpeerucred" != "xyes"; then
	AC_MSG_CHECKING([whether system supports SO_PEERCRED getsockopt])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>]], [[int i = SO_PEERCRED;]])],
		[ AC_MSG_RESULT([yes])
		  AC_DEFINE([HAVE_SO_PEERCRED], [1], [Have PEERCRED socket option])
		], [AC_MSG_RESULT([no])
		NO_PEERCHECK=1
        ])
fi

dnl see whether mkstemp() requires XXXXXX
if test "x$ac_cv_func_mkdtemp" = "xyes" ; then
AC_MSG_CHECKING([for (overly) strict mkstemp])
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([[
#include <stdlib.h>
	]], [[
	char template[]="conftest.mkstemp-test";
	if (mkstemp(template) == -1)
		exit(1);
	unlink(template);
	exit(0);
	]])],
	[
		AC_MSG_RESULT([no])
	],
	[
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_STRICT_MKSTEMP], [1], [Silly mkstemp()])
	],
	[
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_STRICT_MKSTEMP])
	]
)
fi

dnl make sure that openpty does not reacquire controlling terminal
if test ! -z "$check_for_openpty_ctty_bug"; then
	AC_MSG_CHECKING([if openpty correctly handles controlling tty])
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[
#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
		]], [[
	pid_t pid;
	int fd, ptyfd, ttyfd, status;

	pid = fork();
	if (pid < 0) {		/* failed */
		exit(1);
	} else if (pid > 0) {	/* parent */
		waitpid(pid, &status, 0);
		if (WIFEXITED(status))
			exit(WEXITSTATUS(status));
		else
			exit(2);
	} else {		/* child */
		close(0); close(1); close(2);
		setsid();
		openpty(&ptyfd, &ttyfd, NULL, NULL, NULL);
		fd = open("/dev/tty", O_RDWR | O_NOCTTY);
		if (fd >= 0)
			exit(3);	/* Acquired ctty: broken */
		else
			exit(0);	/* Did not acquire ctty: OK */
	}
		]])],
		[
			AC_MSG_RESULT([yes])
		],
		[
			AC_MSG_RESULT([no])
			AC_DEFINE([SSHD_ACQUIRES_CTTY])
		],
		[
			AC_MSG_RESULT([cross-compiling, assuming yes])
		]
	)
fi

if test "x$ac_cv_func_getaddrinfo" = "xyes" && \
    test "x$check_for_hpux_broken_getaddrinfo" = "x1"; then
	AC_MSG_CHECKING([if getaddrinfo seems to work])
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <netinet/in.h>

#define TEST_PORT "2222"
		]], [[
	int err, sock;
	struct addrinfo *gai_ai, *ai, hints;
	char ntop[NI_MAXHOST], strport[NI_MAXSERV], *name = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	err = getaddrinfo(name, TEST_PORT, &hints, &gai_ai);
	if (err != 0) {
		fprintf(stderr, "getaddrinfo failed (%s)", gai_strerror(err));
		exit(1);
	}

	for (ai = gai_ai; ai != NULL; ai = ai->ai_next) {
		if (ai->ai_family != AF_INET6)
			continue;

		err = getnameinfo(ai->ai_addr, ai->ai_addrlen, ntop,
		    sizeof(ntop), strport, sizeof(strport),
		    NI_NUMERICHOST|NI_NUMERICSERV);

		if (err != 0) {
			if (err == EAI_SYSTEM)
				perror("getnameinfo EAI_SYSTEM");
			else
				fprintf(stderr, "getnameinfo failed: %s\n",
				    gai_strerror(err));
			exit(2);
		}

		sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (sock < 0)
			perror("socket");
		if (bind(sock, ai->ai_addr, ai->ai_addrlen) < 0) {
			if (errno == EBADF)
				exit(3);
		}
	}
	exit(0);
		]])],
		[
			AC_MSG_RESULT([yes])
		],
		[
			AC_MSG_RESULT([no])
			AC_DEFINE([BROKEN_GETADDRINFO])
		],
		[
			AC_MSG_RESULT([cross-compiling, assuming yes])
		]
	)
fi

if test "x$ac_cv_func_getaddrinfo" = "xyes" && \
    test "x$check_for_aix_broken_getaddrinfo" = "x1"; then
	AC_MSG_CHECKING([if getaddrinfo seems to work])
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <netinet/in.h>

#define TEST_PORT "2222"
		]], [[
	int err, sock;
	struct addrinfo *gai_ai, *ai, hints;
	char ntop[NI_MAXHOST], strport[NI_MAXSERV], *name = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	err = getaddrinfo(name, TEST_PORT, &hints, &gai_ai);
	if (err != 0) {
		fprintf(stderr, "getaddrinfo failed (%s)", gai_strerror(err));
		exit(1);
	}

	for (ai = gai_ai; ai != NULL; ai = ai->ai_next) {
		if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
			continue;

		err = getnameinfo(ai->ai_addr, ai->ai_addrlen, ntop,
		    sizeof(ntop), strport, sizeof(strport),
		    NI_NUMERICHOST|NI_NUMERICSERV);

		if (ai->ai_family == AF_INET && err != 0) {
			perror("getnameinfo");
			exit(2);
		}
	}
	exit(0);
		]])],
		[
			AC_MSG_RESULT([yes])
			AC_DEFINE([AIX_GETNAMEINFO_HACK], [1],
				[Define if you have a getaddrinfo that fails
				for the all-zeros IPv6 address])
		],
		[
			AC_MSG_RESULT([no])
			AC_DEFINE([BROKEN_GETADDRINFO])
		],
		[
			AC_MSG_RESULT([cross-compiling, assuming no])
		]
	)
fi

if test "x$ac_cv_func_getaddrinfo" = "xyes"; then
	AC_CHECK_DECLS(AI_NUMERICSERV, , ,
	    [#include <sys/types.h>
	     #include <sys/socket.h>
	     #include <netdb.h>])
fi

if test "x$check_for_conflicting_getspnam" = "x1"; then
	AC_MSG_CHECKING([for conflicting getspnam in shadow.h])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <shadow.h> ]],
		[[ exit(0); ]])],
		[
			AC_MSG_RESULT([no])
		],
		[
			AC_MSG_RESULT([yes])
			AC_DEFINE([GETSPNAM_CONFLICTING_DEFS], [1],
			    [Conflicting defs for getspnam])
		]
	)
fi

dnl NetBSD added an strnvis and unfortunately made it incompatible with the
dnl existing one in OpenBSD and Linux's libbsd (the former having existed
dnl for over ten years). Despite this incompatibility being reported during
dnl development (see http://gnats.netbsd.org/44977) they still shipped it.
dnl Even more unfortunately FreeBSD and later MacOS picked up this incompatible
dnl implementation.  Try to detect this mess, and assume the only safe option
dnl if we're cross compiling.
dnl
dnl OpenBSD, 2001: strnvis(char *dst, const char *src, size_t dlen, int flag);
dnl NetBSD: 2012,  strnvis(char *dst, size_t dlen, const char *src, int flag);
if test "x$ac_cv_func_strnvis" = "xyes"; then
	AC_MSG_CHECKING([for working strnvis])
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <vis.h>
static void sighandler(int sig) { _exit(1); }
		]], [[
	char dst[16];

	signal(SIGSEGV, sighandler);
	if (strnvis(dst, "src", 4, 0) && strcmp(dst, "src") == 0)
		exit(0);
	exit(1)
		]])],
		[AC_MSG_RESULT([yes])],
		[AC_MSG_RESULT([no])
		 AC_DEFINE([BROKEN_STRNVIS], [1], [strnvis detected broken])],
		[AC_MSG_WARN([cross compiling: assuming broken])
		 AC_DEFINE([BROKEN_STRNVIS], [1], [strnvis assumed broken])]
	)
fi

AC_CHECK_FUNCS([getpgrp],[
	AC_MSG_CHECKING([if getpgrp accepts zero args])
	AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM([[$ac_includes_default]], [[ getpgrp(); ]])],
		[ AC_MSG_RESULT([yes])
		  AC_DEFINE([GETPGRP_VOID], [1], [getpgrp takes zero args])],
		[ AC_MSG_RESULT([no])
		  AC_DEFINE([GETPGRP_VOID], [0], [getpgrp takes one arg])]
	)
])

# Search for OpenSSL
saved_CPPFLAGS="$CPPFLAGS"
saved_LDFLAGS="$LDFLAGS"
AC_ARG_WITH([ssl-dir],
	[  --with-ssl-dir=PATH     Specify path to OpenSSL installation ],
	[
		if test "x$openssl" = "xno" ; then
			AC_MSG_ERROR([cannot use --with-ssl-dir when OpenSSL disabled])
		fi
		if test "x$withval" != "xno" ; then
			case "$withval" in
				# Relative paths
				./*|../*)	withval="`pwd`/$withval"
			esac
			if test -d "$withval/lib"; then
				if test -n "${need_dash_r}"; then
					LDFLAGS="-L${withval}/lib -R${withval}/lib ${LDFLAGS}"
				else
					LDFLAGS="-L${withval}/lib ${LDFLAGS}"
				fi
			elif test -d "$withval/lib64"; then
				if test -n "${need_dash_r}"; then
					LDFLAGS="-L${withval}/lib64 -R${withval}/lib64 ${LDFLAGS}"
				else
					LDFLAGS="-L${withval}/lib64 ${LDFLAGS}"
				fi
			else
				if test -n "${need_dash_r}"; then
					LDFLAGS="-L${withval} -R${withval} ${LDFLAGS}"
				else
					LDFLAGS="-L${withval} ${LDFLAGS}"
				fi
			fi
			if test -d "$withval/include"; then
				CPPFLAGS="-I${withval}/include ${CPPFLAGS}"
			else
				CPPFLAGS="-I${withval} ${CPPFLAGS}"
			fi
		fi
	]
)

AC_ARG_WITH([openssl-header-check],
	[  --without-openssl-header-check Disable OpenSSL version consistency check],
	[
		if test "x$withval" = "xno" ; then
			openssl_check_nonfatal=1
		fi
	]
)

openssl_engine=no
AC_ARG_WITH([ssl-engine],
	[  --with-ssl-engine       Enable OpenSSL (hardware) ENGINE support ],
	[
		if test "x$withval" != "xno" ; then
			if test "x$openssl" = "xno" ; then
				AC_MSG_ERROR([cannot use --with-ssl-engine when OpenSSL disabled])
			fi
			openssl_engine=yes
		fi
	]
)

if test "x$openssl" = "xyes" ; then
	LIBS="-lcrypto $LIBS"
	AC_TRY_LINK_FUNC([RAND_add], [AC_DEFINE([HAVE_OPENSSL], [1],
		[Define if your ssl headers are included
		with #include <openssl/header.h>])],
		[
			dnl Check default openssl install dir
			if test -n "${need_dash_r}"; then
				LDFLAGS="-L/usr/local/ssl/lib -R/usr/local/ssl/lib ${saved_LDFLAGS}"
			else
				LDFLAGS="-L/usr/local/ssl/lib ${saved_LDFLAGS}"
			fi
			CPPFLAGS="-I/usr/local/ssl/include ${saved_CPPFLAGS}"
			AC_CHECK_HEADER([openssl/opensslv.h], ,
			    [AC_MSG_ERROR([*** OpenSSL headers missing - please install first or check config.log ***])])
			AC_TRY_LINK_FUNC([RAND_add], [AC_DEFINE([HAVE_OPENSSL])],
				[
					AC_MSG_ERROR([*** Can't find recent OpenSSL libcrypto (see config.log for details) ***])
				]
			)
		]
	)

	# Determine OpenSSL header version
	AC_MSG_CHECKING([OpenSSL header version])
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[
	#include <stdlib.h>
	#include <stdio.h>
	#include <string.h>
	#include <openssl/opensslv.h>
	#define DATA "conftest.sslincver"
		]], [[
		FILE *fd;
		int rc;

		fd = fopen(DATA,"w");
		if(fd == NULL)
			exit(1);

		if ((rc = fprintf(fd, "%08lx (%s)\n",
		    (unsigned long)OPENSSL_VERSION_NUMBER,
		     OPENSSL_VERSION_TEXT)) < 0)
			exit(1);

		exit(0);
		]])],
		[
			ssl_header_ver=`cat conftest.sslincver`
			AC_MSG_RESULT([$ssl_header_ver])
		],
		[
			AC_MSG_RESULT([not found])
			AC_MSG_ERROR([OpenSSL version header not found.])
		],
		[
			AC_MSG_WARN([cross compiling: not checking])
		]
	)

	# Determine OpenSSL library version
	AC_MSG_CHECKING([OpenSSL library version])
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[
	#include <stdio.h>
	#include <string.h>
	#include <openssl/opensslv.h>
	#include <openssl/crypto.h>
	#define DATA "conftest.ssllibver"
		]], [[
		FILE *fd;
		int rc;

		fd = fopen(DATA,"w");
		if(fd == NULL)
			exit(1);

		if ((rc = fprintf(fd, "%08lx (%s)\n", (unsigned long)SSLeay(),
		    SSLeay_version(SSLEAY_VERSION))) < 0)
			exit(1);

		exit(0);
		]])],
		[
			ssl_library_ver=`cat conftest.ssllibver`
			# Check version is supported.
			case "$ssl_library_ver" in
				10000*|0*)
					AC_MSG_ERROR([OpenSSL >= 1.0.1 required (have "$ssl_library_ver")])
			                ;;
				100*)   ;; # 1.0.x
				101*)   ;; # 1.1.x
				200*)   ;; # LibreSSL
			        *)
					AC_MSG_ERROR([OpenSSL > 1.1.x is not yet supported (have "$ssl_library_ver")])
			                ;;
			esac
			AC_MSG_RESULT([$ssl_library_ver])
		],
		[
			AC_MSG_RESULT([not found])
			AC_MSG_ERROR([OpenSSL library not found.])
		],
		[
			AC_MSG_WARN([cross compiling: not checking])
		]
	)

	# Sanity check OpenSSL headers
	AC_MSG_CHECKING([whether OpenSSL's headers match the library])
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[
	#include <string.h>
	#include <openssl/opensslv.h>
	#include <openssl/crypto.h>
		]], [[
		exit(SSLeay() == OPENSSL_VERSION_NUMBER ? 0 : 1);
		]])],
		[
			AC_MSG_RESULT([yes])
		],
		[
			AC_MSG_RESULT([no])
			if test "x$openssl_check_nonfatal" = "x"; then
				AC_MSG_ERROR([Your OpenSSL headers do not match your
	library. Check config.log for details.
	If you are sure your installation is consistent, you can disable the check
	by running "./configure --without-openssl-header-check".
	Also see contrib/findssl.sh for help identifying header/library mismatches.
	])
			else
				AC_MSG_WARN([Your OpenSSL headers do not match your
	library. Check config.log for details.
	Also see contrib/findssl.sh for help identifying header/library mismatches.])
			fi
		],
		[
			AC_MSG_WARN([cross compiling: not checking])
		]
	)

	AC_MSG_CHECKING([if programs using OpenSSL functions will link])
	AC_LINK_IFELSE(
		[AC_LANG_PROGRAM([[ #include <openssl/evp.h> ]],
		[[ SSLeay_add_all_algorithms(); ]])],
		[
			AC_MSG_RESULT([yes])
		],
		[
			AC_MSG_RESULT([no])
			saved_LIBS="$LIBS"
			LIBS="$LIBS -ldl"
			AC_MSG_CHECKING([if programs using OpenSSL need -ldl])
			AC_LINK_IFELSE(
				[AC_LANG_PROGRAM([[ #include <openssl/evp.h> ]],
				[[ SSLeay_add_all_algorithms(); ]])],
				[
					AC_MSG_RESULT([yes])
				],
				[
					AC_MSG_RESULT([no])
					LIBS="$saved_LIBS"
				]
			)
		]
	)

	AC_CHECK_FUNCS([ \
		BN_is_prime_ex \
		DSA_generate_parameters_ex \
		EVP_DigestInit_ex \
		EVP_DigestFinal_ex \
		EVP_MD_CTX_init \
		EVP_MD_CTX_cleanup \
		EVP_MD_CTX_copy_ex \
		HMAC_CTX_init \
		RSA_generate_key_ex \
		RSA_get_default_method \
	])

	if test "x$openssl_engine" = "xyes" ; then
		AC_MSG_CHECKING([for OpenSSL ENGINE support])
		AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
	#include <openssl/engine.h>
			]], [[
				ENGINE_load_builtin_engines();
				ENGINE_register_all_complete();
			]])],
			[ AC_MSG_RESULT([yes])
			  AC_DEFINE([USE_OPENSSL_ENGINE], [1],
			     [Enable OpenSSL engine support])
			], [ AC_MSG_ERROR([OpenSSL ENGINE support not found])
		])
	fi

	# Check for OpenSSL without EVP_aes_{192,256}_cbc
	AC_MSG_CHECKING([whether OpenSSL has crippled AES support])
	AC_LINK_IFELSE(
		[AC_LANG_PROGRAM([[
	#include <string.h>
	#include <openssl/evp.h>
		]], [[
		exit(EVP_aes_192_cbc() == NULL || EVP_aes_256_cbc() == NULL);
		]])],
		[
			AC_MSG_RESULT([no])
		],
		[
			AC_MSG_RESULT([yes])
			AC_DEFINE([OPENSSL_LOBOTOMISED_AES], [1],
			    [libcrypto is missing AES 192 and 256 bit functions])
		]
	)

	# Check for OpenSSL with EVP_aes_*ctr
	AC_MSG_CHECKING([whether OpenSSL has AES CTR via EVP])
	AC_LINK_IFELSE(
		[AC_LANG_PROGRAM([[
	#include <string.h>
	#include <openssl/evp.h>
		]], [[
		exit(EVP_aes_128_ctr() == NULL ||
		    EVP_aes_192_cbc() == NULL ||
		    EVP_aes_256_cbc() == NULL);
		]])],
		[
			AC_MSG_RESULT([yes])
			AC_DEFINE([OPENSSL_HAVE_EVPCTR], [1],
			    [libcrypto has EVP AES CTR])
		],
		[
			AC_MSG_RESULT([no])
		]
	)

	# Check for OpenSSL with EVP_aes_*gcm
	AC_MSG_CHECKING([whether OpenSSL has AES GCM via EVP])
	AC_LINK_IFELSE(
		[AC_LANG_PROGRAM([[
	#include <string.h>
	#include <openssl/evp.h>
		]], [[
		exit(EVP_aes_128_gcm() == NULL ||
		    EVP_aes_256_gcm() == NULL ||
		    EVP_CTRL_GCM_SET_IV_FIXED == 0 ||
		    EVP_CTRL_GCM_IV_GEN == 0 ||
		    EVP_CTRL_GCM_SET_TAG == 0 ||
		    EVP_CTRL_GCM_GET_TAG == 0 ||
		    EVP_CIPHER_CTX_ctrl(NULL, 0, 0, NULL) == 0);
		]])],
		[
			AC_MSG_RESULT([yes])
			AC_DEFINE([OPENSSL_HAVE_EVPGCM], [1],
			    [libcrypto has EVP AES GCM])
		],
		[
			AC_MSG_RESULT([no])
			unsupported_algorithms="$unsupported_cipers \
			   aes128-gcm@openssh.com \
			   aes256-gcm@openssh.com"
		]
	)

	AC_SEARCH_LIBS([EVP_CIPHER_CTX_ctrl], [crypto],
		[AC_DEFINE([HAVE_EVP_CIPHER_CTX_CTRL], [1],
		    [Define if libcrypto has EVP_CIPHER_CTX_ctrl])])

	# LibreSSL/OpenSSL 1.1x API
	AC_SEARCH_LIBS([DH_get0_key], [crypto],
		[AC_DEFINE([HAVE_DH_GET0_KEY], [1],
		    [Define if libcrypto has DH_get0_key])])
	AC_SEARCH_LIBS([DH_get0_pqg], [crypto],
		[AC_DEFINE([HAVE_DH_GET0_PQG], [1],
		    [Define if libcrypto has DH_get0_pqg])])
	AC_SEARCH_LIBS([DH_set0_key], [crypto],
		[AC_DEFINE([HAVE_DH_SET0_KEY], [1],
		    [Define if libcrypto has DH_set0_key])])
	AC_SEARCH_LIBS([DH_set_length], [crypto],
		[AC_DEFINE([HAVE_DH_SET_LENGTH], [1],
		    [Define if libcrypto has DH_set_length])])
	AC_SEARCH_LIBS([DH_set0_pqg], [crypto],
		[AC_DEFINE([HAVE_DH_SET0_PQG], [1],
		    [Define if libcrypto has DH_set0_pqg])])

	AC_SEARCH_LIBS([DSA_get0_key], [crypto],
		[AC_DEFINE([HAVE_DSA_GET0_KEY], [1],
		    [Define if libcrypto has DSA_get0_key])])
	AC_SEARCH_LIBS([DSA_get0_pqg], [crypto],
		[AC_DEFINE([HAVE_DSA_GET0_PQG], [1],
		    [Define if libcrypto has DSA_get0_pqg])])
	AC_SEARCH_LIBS([DSA_set0_key], [crypto],
		[AC_DEFINE([HAVE_DSA_SET0_KEY], [1],
		    [Define if libcrypto has DSA_set0_key])])
	AC_SEARCH_LIBS([DSA_set0_pqg], [crypto],
		[AC_DEFINE([HAVE_DSA_SET0_PQG], [1],
		    [Define if libcrypto has DSA_set0_pqg])])

	AC_SEARCH_LIBS([DSA_SIG_get0], [crypto],
		[AC_DEFINE([HAVE_DSA_SIG_GET0], [1],
		    [Define if libcrypto has DSA_SIG_get0])])
	AC_SEARCH_LIBS([DSA_SIG_set0], [crypto],
		[AC_DEFINE([HAVE_DSA_SIG_SET0], [1],
		    [Define if libcrypto has DSA_SIG_set0])])

	AC_SEARCH_LIBS([ECDSA_SIG_get0], [crypto],
		[AC_DEFINE([HAVE_ECDSA_SIG_GET0], [1],
		    [Define if libcrypto has ECDSA_SIG_get0])])
	AC_SEARCH_LIBS([ECDSA_SIG_set0], [crypto],
		[AC_DEFINE([HAVE_ECDSA_SIG_SET0], [1],
		    [Define if libcrypto has ECDSA_SIG_set0])])

	AC_SEARCH_LIBS([EVP_CIPHER_CTX_iv], [crypto],
		[AC_DEFINE([HAVE_EVP_CIPHER_CTX_IV], [1],
		    [Define if libcrypto has EVP_CIPHER_CTX_iv])])
	AC_SEARCH_LIBS([EVP_CIPHER_CTX_iv_noconst], [crypto],
		[AC_DEFINE([HAVE_EVP_CIPHER_CTX_IV_NOCONST], [1],
		    [Define if libcrypto has EVP_CIPHER_CTX_iv_noconst])])
	AC_SEARCH_LIBS([EVP_CIPHER_CTX_get_iv], [crypto],
		[AC_DEFINE([HAVE_EVP_CIPHER_CTX_GET_IV], [1],
		    [Define if libcrypto has EVP_CIPHER_CTX_get_iv])])
	AC_SEARCH_LIBS([EVP_CIPHER_CTX_set_iv], [crypto],
		[AC_DEFINE([HAVE_EVP_CIPHER_CTX_GET_IV], [1],
		    [Define if libcrypto has EVP_CIPHER_CTX_set_iv])])

	AC_SEARCH_LIBS([RSA_get0_crt_params], [crypto],
		[AC_DEFINE([HAVE_RSA_GET0_CRT_PARAMS], [1],
		    [Define if libcrypto has RSA_get0_crt_params])])
	AC_SEARCH_LIBS([RSA_get0_factors], [crypto],
		[AC_DEFINE([HAVE_RSA_GET0_FACTORS], [1],
		    [Define if libcrypto has RSA_get0_factors])])
	AC_SEARCH_LIBS([RSA_get0_key], [crypto],
		[AC_DEFINE([HAVE_RSA_GET0_KEY], [1],
		    [Define if libcrypto has RSA_get0_key])])
	AC_SEARCH_LIBS([RSA_set0_crt_params], [crypto],
		[AC_DEFINE([HAVE_RSA_SET0_CRT_PARAMS], [1],
		    [Define if libcrypto has RSA_get0_srt_params])])
	AC_SEARCH_LIBS([RSA_set0_factors], [crypto],
		[AC_DEFINE([HAVE_RSA_SET0_FACTORS], [1],
		    [Define if libcrypto has RSA_set0_factors])])
	AC_SEARCH_LIBS([RSA_set0_key], [crypto],
		[AC_DEFINE([HAVE_RSA_SET0_KEY], [1],
		    [Define if libcrypto has RSA_set0_key])])

	AC_SEARCH_LIBS([RSA_meth_free], [crypto],
		[AC_DEFINE([HAVE_RSA_METH_FREE], [1],
		    [Define if libcrypto has RSA_meth_free])])
	AC_SEARCH_LIBS([RSA_meth_dup], [crypto],
		[AC_DEFINE([HAVE_RSA_METH_DUP], [1],
		    [Define if libcrypto has RSA_meth_dup])])
	AC_SEARCH_LIBS([RSA_meth_set1_name], [crypto],
		[AC_DEFINE([HAVE_RSA_METH_SET1_NAME], [1],
		    [Define if libcrypto has RSA_meth_set1_name])])
	AC_SEARCH_LIBS([RSA_meth_get_finish], [crypto],
		[AC_DEFINE([HAVE_RSA_METH_GET_FINISH], [1],
		    [Define if libcrypto has RSA_meth_get_finish])])
	AC_SEARCH_LIBS([RSA_meth_set_priv_enc], [crypto],
		[AC_DEFINE([HAVE_RSA_METH_SET_PRIV_ENC], [1],
		    [Define if libcrypto has RSA_meth_set_priv_enc])])
	AC_SEARCH_LIBS([RSA_meth_set_priv_dec], [crypto],
		[AC_DEFINE([HAVE_RSA_METH_SET_PRIV_DEC], [1],
		    [Define if libcrypto has RSA_meth_set_priv_dec])])
	AC_SEARCH_LIBS([RSA_meth_set_finish], [crypto],
		[AC_DEFINE([HAVE_RSA_METH_SET_FINISH], [1],
		    [Define if libcrypto has RSA_meth_set_finish])])

	AC_SEARCH_LIBS([EVP_PKEY_get0_RSA], [crypto],
		[AC_DEFINE([HAVE_EVP_PKEY_GET0_RSA], [1],
		    [Define if libcrypto has EVP_PKEY_get0_RSA])])

	AC_SEARCH_LIBS([EVP_MD_CTX_new], [crypto],
		[AC_DEFINE([HAVE_EVP_MD_CTX_NEW], [1],
		    [Define if libcrypto has EVP_MD_CTX_new])])
	AC_SEARCH_LIBS([EVP_MD_CTX_free], [crypto],
		[AC_DEFINE([HAVE_EVP_MD_CTX_FREE], [1],
		    [Define if libcrypto has EVP_MD_CTX_free])])

	AC_MSG_CHECKING([if EVP_DigestUpdate returns an int])
	AC_LINK_IFELSE(
		[AC_LANG_PROGRAM([[
	#include <string.h>
	#include <openssl/evp.h>
		]], [[
		if(EVP_DigestUpdate(NULL, NULL,0))
			exit(0);
		]])],
		[
			AC_MSG_RESULT([yes])
		],
		[
			AC_MSG_RESULT([no])
			AC_DEFINE([OPENSSL_EVP_DIGESTUPDATE_VOID], [1],
			    [Define if EVP_DigestUpdate returns void])
		]
	)

	# Some systems want crypt() from libcrypt, *not* the version in OpenSSL,
	# because the system crypt() is more featureful.
	if test "x$check_for_libcrypt_before" = "x1"; then
		AC_CHECK_LIB([crypt], [crypt])
	fi

	# Some Linux systems (Slackware) need crypt() from libcrypt, *not* the
	# version in OpenSSL.
	if test "x$check_for_libcrypt_later" = "x1"; then
		AC_CHECK_LIB([crypt], [crypt], [LIBS="$LIBS -lcrypt"])
	fi
	AC_CHECK_FUNCS([crypt DES_crypt])

	# Search for SHA256 support in libc and/or OpenSSL
	AC_CHECK_FUNCS([SHA256_Update EVP_sha256], ,
	    [unsupported_algorithms="$unsupported_algorithms \
		hmac-sha2-256 \
		hmac-sha2-512 \
		diffie-hellman-group-exchange-sha256 \
		hmac-sha2-256-etm@openssh.com \
		hmac-sha2-512-etm@openssh.com"
	     ]
	)
	# Search for RIPE-MD support in OpenSSL
	AC_CHECK_FUNCS([EVP_ripemd160], ,
	    [unsupported_algorithms="$unsupported_algorithms \
		hmac-ripemd160 \
		hmac-ripemd160@openssh.com \
		hmac-ripemd160-etm@openssh.com"
	     ]
	)

	# Check complete ECC support in OpenSSL
	AC_MSG_CHECKING([whether OpenSSL has NID_X9_62_prime256v1])
	AC_LINK_IFELSE(
		[AC_LANG_PROGRAM([[
	#include <openssl/ec.h>
	#include <openssl/ecdh.h>
	#include <openssl/ecdsa.h>
	#include <openssl/evp.h>
	#include <openssl/objects.h>
	#include <openssl/opensslv.h>
		]], [[
		EC_KEY *e = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
		const EVP_MD *m = EVP_sha256(); /* We need this too */
		]])],
		[ AC_MSG_RESULT([yes])
		  enable_nistp256=1 ],
		[ AC_MSG_RESULT([no]) ]
	)

	AC_MSG_CHECKING([whether OpenSSL has NID_secp384r1])
	AC_LINK_IFELSE(
		[AC_LANG_PROGRAM([[
	#include <openssl/ec.h>
	#include <openssl/ecdh.h>
	#include <openssl/ecdsa.h>
	#include <openssl/evp.h>
	#include <openssl/objects.h>
	#include <openssl/opensslv.h>
		]], [[
		EC_KEY *e = EC_KEY_new_by_curve_name(NID_secp384r1);
		const EVP_MD *m = EVP_sha384(); /* We need this too */
		]])],
		[ AC_MSG_RESULT([yes])
		  enable_nistp384=1 ],
		[ AC_MSG_RESULT([no]) ]
	)

	AC_MSG_CHECKING([whether OpenSSL has NID_secp521r1])
	AC_LINK_IFELSE(
		[AC_LANG_PROGRAM([[
	#include <openssl/ec.h>
	#include <openssl/ecdh.h>
	#include <openssl/ecdsa.h>
	#include <openssl/evp.h>
	#include <openssl/objects.h>
	#include <openssl/opensslv.h>
		]], [[
		EC_KEY *e = EC_KEY_new_by_curve_name(NID_secp521r1);
		const EVP_MD *m = EVP_sha512(); /* We need this too */
		]])],
		[ AC_MSG_RESULT([yes])
		  AC_MSG_CHECKING([if OpenSSL's NID_secp521r1 is functional])
		  AC_RUN_IFELSE(
			[AC_LANG_PROGRAM([[
	#include <openssl/ec.h>
	#include <openssl/ecdh.h>
	#include <openssl/ecdsa.h>
	#include <openssl/evp.h>
	#include <openssl/objects.h>
	#include <openssl/opensslv.h>
			]],[[
			EC_KEY *e = EC_KEY_new_by_curve_name(NID_secp521r1);
			const EVP_MD *m = EVP_sha512(); /* We need this too */
			exit(e == NULL || m == NULL);
			]])],
			[ AC_MSG_RESULT([yes])
			  enable_nistp521=1 ],
			[ AC_MSG_RESULT([no]) ],
			[ AC_MSG_WARN([cross-compiling: assuming yes])
			  enable_nistp521=1 ]
		  )],
		AC_MSG_RESULT([no])
	)

	COMMENT_OUT_ECC="#no ecc#"
	TEST_SSH_ECC=no

	if test x$enable_nistp256 = x1 || test x$enable_nistp384 = x1 || \
	    test x$enable_nistp521 = x1; then
		AC_DEFINE(OPENSSL_HAS_ECC, [1], [OpenSSL has ECC])
	fi
	if test x$enable_nistp256 = x1; then
		AC_DEFINE([OPENSSL_HAS_NISTP256], [1],
		    [libcrypto has NID_X9_62_prime256v1])
		TEST_SSH_ECC=yes
		COMMENT_OUT_ECC=""
	else
		unsupported_algorithms="$unsupported_algorithms \
			ecdsa-sha2-nistp256 \
			ecdh-sha2-nistp256 \
			ecdsa-sha2-nistp256-cert-v01@openssh.com"
	fi
	if test x$enable_nistp384 = x1; then
		AC_DEFINE([OPENSSL_HAS_NISTP384], [1], [libcrypto has NID_secp384r1])
		TEST_SSH_ECC=yes
		COMMENT_OUT_ECC=""
	else
		unsupported_algorithms="$unsupported_algorithms \
			ecdsa-sha2-nistp384 \
			ecdh-sha2-nistp384 \
			ecdsa-sha2-nistp384-cert-v01@openssh.com"
	fi
	if test x$enable_nistp521 = x1; then
		AC_DEFINE([OPENSSL_HAS_NISTP521], [1], [libcrypto has NID_secp521r1])
		TEST_SSH_ECC=yes
		COMMENT_OUT_ECC=""
	else
		unsupported_algorithms="$unsupported_algorithms \
			ecdh-sha2-nistp521 \
			ecdsa-sha2-nistp521 \
			ecdsa-sha2-nistp521-cert-v01@openssh.com"
	fi

	AC_SUBST([TEST_SSH_ECC])
	AC_SUBST([COMMENT_OUT_ECC])
else
	AC_CHECK_LIB([crypt], [crypt], [LIBS="$LIBS -lcrypt"])
	AC_CHECK_FUNCS([crypt])
fi

AC_CHECK_FUNCS([ \
	arc4random \
	arc4random_buf \
	arc4random_stir \
	arc4random_uniform \
])

saved_LIBS="$LIBS"
AC_CHECK_LIB([iaf], [ia_openinfo], [
	LIBS="$LIBS -liaf"
	AC_CHECK_FUNCS([set_id], [SSHDLIBS="$SSHDLIBS -liaf"
				AC_DEFINE([HAVE_LIBIAF], [1],
			[Define if system has libiaf that supports set_id])
				])
])
LIBS="$saved_LIBS"

### Configure cryptographic random number support

# Check whether OpenSSL seeds itself
if test "x$openssl" = "xyes" ; then
	AC_MSG_CHECKING([whether OpenSSL's PRNG is internally seeded])
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[
	#include <string.h>
	#include <openssl/rand.h>
		]], [[
		exit(RAND_status() == 1 ? 0 : 1);
		]])],
		[
			OPENSSL_SEEDS_ITSELF=yes
			AC_MSG_RESULT([yes])
		],
		[
			AC_MSG_RESULT([no])
		],
		[
			AC_MSG_WARN([cross compiling: assuming yes])
			# This is safe, since we will fatal() at runtime if
			# OpenSSL is not seeded correctly.
			OPENSSL_SEEDS_ITSELF=yes
		]
	)
fi

# PRNGD TCP socket
AC_ARG_WITH([prngd-port],
	[  --with-prngd-port=PORT  read entropy from PRNGD/EGD TCP localhost:PORT],
	[
		case "$withval" in
		no)
			withval=""
			;;
		[[0-9]]*)
			;;
		*)
			AC_MSG_ERROR([You must specify a numeric port number for --with-prngd-port])
			;;
		esac
		if test ! -z "$withval" ; then
			PRNGD_PORT="$withval"
			AC_DEFINE_UNQUOTED([PRNGD_PORT], [$PRNGD_PORT],
				[Port number of PRNGD/EGD random number socket])
		fi
	]
)

# PRNGD Unix domain socket
AC_ARG_WITH([prngd-socket],
	[  --with-prngd-socket=FILE read entropy from PRNGD/EGD socket FILE (default=/var/run/egd-pool)],
	[
		case "$withval" in
		yes)
			withval="/var/run/egd-pool"
			;;
		no)
			withval=""
			;;
		/*)
			;;
		*)
			AC_MSG_ERROR([You must specify an absolute path to the entropy socket])
			;;
		esac

		if test ! -z "$withval" ; then
			if test ! -z "$PRNGD_PORT" ; then
				AC_MSG_ERROR([You may not specify both a PRNGD/EGD port and socket])
			fi
			if test ! -r "$withval" ; then
				AC_MSG_WARN([Entropy socket is not readable])
			fi
			PRNGD_SOCKET="$withval"
			AC_DEFINE_UNQUOTED([PRNGD_SOCKET], ["$PRNGD_SOCKET"],
				[Location of PRNGD/EGD random number socket])
		fi
	],
	[
		# Check for existing socket only if we don't have a random device already
		if test "x$OPENSSL_SEEDS_ITSELF" != "xyes" ; then
			AC_MSG_CHECKING([for PRNGD/EGD socket])
			# Insert other locations here
			for sock in /var/run/egd-pool /dev/egd-pool /etc/entropy; do
				if test -r $sock && $TEST_MINUS_S_SH -c "test -S $sock -o -p $sock" ; then
					PRNGD_SOCKET="$sock"
					AC_DEFINE_UNQUOTED([PRNGD_SOCKET], ["$PRNGD_SOCKET"])
					break;
				fi
			done
			if test ! -z "$PRNGD_SOCKET" ; then
				AC_MSG_RESULT([$PRNGD_SOCKET])
			else
				AC_MSG_RESULT([not found])
			fi
		fi
	]
)

# Which randomness source do we use?
if test ! -z "$PRNGD_PORT" ; then
	RAND_MSG="PRNGd port $PRNGD_PORT"
elif test ! -z "$PRNGD_SOCKET" ; then
	RAND_MSG="PRNGd socket $PRNGD_SOCKET"
elif test ! -z "$OPENSSL_SEEDS_ITSELF" ; then
	AC_DEFINE([OPENSSL_PRNG_ONLY], [1],
		[Define if you want the OpenSSL internally seeded PRNG only])
	RAND_MSG="OpenSSL internal ONLY"
elif test "x$openssl" = "xno" ; then
	AC_MSG_WARN([OpenSSH will use /dev/urandom as a source of random numbers. It will fail if this device is not supported or accessible])
else
	AC_MSG_ERROR([OpenSSH has no source of random numbers. Please configure OpenSSL with an entropy source or re-run configure using one of the --with-prngd-port or --with-prngd-socket options])
fi

# Check for PAM libs
PAM_MSG="no"
AC_ARG_WITH([pam],
	[  --with-pam              Enable PAM support ],
	[
		if test "x$withval" != "xno" ; then
			if test "x$ac_cv_header_security_pam_appl_h" != "xyes" && \
			   test "x$ac_cv_header_pam_pam_appl_h" != "xyes" ; then
				AC_MSG_ERROR([PAM headers not found])
			fi

			saved_LIBS="$LIBS"
			AC_CHECK_LIB([dl], [dlopen], , )
			AC_CHECK_LIB([pam], [pam_set_item], , [AC_MSG_ERROR([*** libpam missing])])
			AC_CHECK_FUNCS([pam_getenvlist])
			AC_CHECK_FUNCS([pam_putenv])
			LIBS="$saved_LIBS"

			PAM_MSG="yes"

			SSHDLIBS="$SSHDLIBS -lpam"
			AC_DEFINE([USE_PAM], [1],
				[Define if you want to enable PAM support])

			if test $ac_cv_lib_dl_dlopen = yes; then
				case "$LIBS" in
				*-ldl*)
					# libdl already in LIBS
					;;
				*)
					SSHDLIBS="$SSHDLIBS -ldl"
					;;
				esac
			fi
		fi
	]
)

AC_ARG_WITH([pam-service],
	[  --with-pam-service=name Specify PAM service name ],
	[
		if test "x$withval" != "xno" && \
		   test "x$withval" != "xyes" ; then
			AC_DEFINE_UNQUOTED([SSHD_PAM_SERVICE],
				["$withval"], [sshd PAM service name])
		fi
	]
)

# Check for older PAM
if test "x$PAM_MSG" = "xyes" ; then
	# Check PAM strerror arguments (old PAM)
	AC_MSG_CHECKING([whether pam_strerror takes only one argument])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <stdlib.h>
#if defined(HAVE_SECURITY_PAM_APPL_H)
#include <security/pam_appl.h>
#elif defined (HAVE_PAM_PAM_APPL_H)
#include <pam/pam_appl.h>
#endif
		]], [[
(void)pam_strerror((pam_handle_t *)NULL, -1);
		]])], [AC_MSG_RESULT([no])], [
			AC_DEFINE([HAVE_OLD_PAM], [1],
				[Define if you have an old version of PAM
				which takes only one argument to pam_strerror])
			AC_MSG_RESULT([yes])
			PAM_MSG="yes (old library)"

	])
fi

case "$host" in
*-*-cygwin*)
	SSH_PRIVSEP_USER=CYGWIN_SSH_PRIVSEP_USER
	;;
*)
	SSH_PRIVSEP_USER=sshd
	;;
esac
AC_ARG_WITH([privsep-user],
	[  --with-privsep-user=user Specify non-privileged user for privilege separation],
	[
		if test -n "$withval"  &&  test "x$withval" != "xno"  &&  \
		    test "x${withval}" != "xyes"; then
			SSH_PRIVSEP_USER=$withval
		fi
	]
)
if test "x$SSH_PRIVSEP_USER" = "xCYGWIN_SSH_PRIVSEP_USER" ; then
	AC_DEFINE_UNQUOTED([SSH_PRIVSEP_USER], [CYGWIN_SSH_PRIVSEP_USER],
		[Cygwin function to fetch non-privileged user for privilege separation])
else
	AC_DEFINE_UNQUOTED([SSH_PRIVSEP_USER], ["$SSH_PRIVSEP_USER"],
		[non-privileged user for privilege separation])
fi
AC_SUBST([SSH_PRIVSEP_USER])

if test "x$have_linux_no_new_privs" = "x1" ; then
AC_CHECK_DECL([SECCOMP_MODE_FILTER], [have_seccomp_filter=1], , [
	#include <sys/types.h>
	#include <linux/seccomp.h>
])
fi
if test "x$have_seccomp_filter" = "x1" ; then
AC_MSG_CHECKING([kernel for seccomp_filter support])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
		#include <errno.h>
		#include <elf.h>
		#include <linux/audit.h>
		#include <linux/seccomp.h>
		#include <stdlib.h>
		#include <sys/prctl.h>
	]],
	[[ int i = $seccomp_audit_arch;
	   errno = 0;
	   prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, NULL, 0, 0);
	   exit(errno == EFAULT ? 0 : 1); ]])],
	[ AC_MSG_RESULT([yes]) ], [
		AC_MSG_RESULT([no])
		# Disable seccomp filter as a target
		have_seccomp_filter=0
	]
)
fi

# Decide which sandbox style to use
sandbox_arg=""
AC_ARG_WITH([sandbox],
	[  --with-sandbox=style    Specify privilege separation sandbox (no, capsicum, darwin, rlimit, seccomp_filter, systrace, pledge)],
	[
		if test "x$withval" = "xyes" ; then
			sandbox_arg=""
		else
			sandbox_arg="$withval"
		fi
	]
)

# Some platforms (seems to be the ones that have a kernel poll(2)-type
# function with which they implement select(2)) use an extra file descriptor
# when calling select(2), which means we can't use the rlimit sandbox.
AC_MSG_CHECKING([if select works with descriptor rlimit])
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([[
#include <sys/types.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#include <sys/resource.h>
#ifdef HAVE_SYS_SELECT_H
# include <sys/select.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
	]],[[
	struct rlimit rl_zero;
	int fd, r;
	fd_set fds;
	struct timeval tv;

	fd = open("/dev/null", O_RDONLY);
	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	rl_zero.rlim_cur = rl_zero.rlim_max = 0;
	setrlimit(RLIMIT_FSIZE, &rl_zero);
	setrlimit(RLIMIT_NOFILE, &rl_zero);
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	r = select(fd+1, &fds, NULL, NULL, &tv);
	exit (r == -1 ? 1 : 0);
	]])],
	[AC_MSG_RESULT([yes])
	 select_works_with_rlimit=yes],
	[AC_MSG_RESULT([no])
	 select_works_with_rlimit=no],
	[AC_MSG_WARN([cross compiling: assuming yes])
	 select_works_with_rlimit=yes]
)

AC_MSG_CHECKING([if setrlimit(RLIMIT_NOFILE,{0,0}) works])
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([[
#include <sys/types.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#include <sys/resource.h>
#include <errno.h>
#include <stdlib.h>
	]],[[
	struct rlimit rl_zero;
	int fd, r;
	fd_set fds;

	rl_zero.rlim_cur = rl_zero.rlim_max = 0;
	r = setrlimit(RLIMIT_NOFILE, &rl_zero);
	exit (r == -1 ? 1 : 0);
	]])],
	[AC_MSG_RESULT([yes])
	 rlimit_nofile_zero_works=yes],
	[AC_MSG_RESULT([no])
	 rlimit_nofile_zero_works=no],
	[AC_MSG_WARN([cross compiling: assuming yes])
	 rlimit_nofile_zero_works=yes]
)

AC_MSG_CHECKING([if setrlimit RLIMIT_FSIZE works])
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/resource.h>
#include <stdlib.h>
	]],[[
		struct rlimit rl_zero;

		rl_zero.rlim_cur = rl_zero.rlim_max = 0;
		exit(setrlimit(RLIMIT_FSIZE, &rl_zero) != 0);
	]])],
	[AC_MSG_RESULT([yes])],
	[AC_MSG_RESULT([no])
	 AC_DEFINE(SANDBOX_SKIP_RLIMIT_FSIZE, 1,
	    [setrlimit RLIMIT_FSIZE works])],
	[AC_MSG_WARN([cross compiling: assuming yes])]
)

if test "x$sandbox_arg" = "xpledge" || \
   ( test -z "$sandbox_arg" && test "x$ac_cv_func_pledge" = "xyes" ) ; then
	test "x$ac_cv_func_pledge" != "xyes" && \
		AC_MSG_ERROR([pledge sandbox requires pledge(2) support])
	SANDBOX_STYLE="pledge"
	AC_DEFINE([SANDBOX_PLEDGE], [1], [Sandbox using pledge(2)])
elif test "x$sandbox_arg" = "xsystrace" || \
   ( test -z "$sandbox_arg" && test "x$have_systr_policy_kill" = "x1" ) ; then
	test "x$have_systr_policy_kill" != "x1" && \
		AC_MSG_ERROR([systrace sandbox requires systrace headers and SYSTR_POLICY_KILL support])
	SANDBOX_STYLE="systrace"
	AC_DEFINE([SANDBOX_SYSTRACE], [1], [Sandbox using systrace(4)])
elif test "x$sandbox_arg" = "xdarwin" || \
     ( test -z "$sandbox_arg" && test "x$ac_cv_func_sandbox_init" = "xyes" && \
       test "x$ac_cv_header_sandbox_h" = "xyes") ; then
	test "x$ac_cv_func_sandbox_init" != "xyes" -o \
	     "x$ac_cv_header_sandbox_h" != "xyes" && \
		AC_MSG_ERROR([Darwin seatbelt sandbox requires sandbox.h and sandbox_init function])
	SANDBOX_STYLE="darwin"
	AC_DEFINE([SANDBOX_DARWIN], [1], [Sandbox using Darwin sandbox_init(3)])
elif test "x$sandbox_arg" = "xseccomp_filter" || \
     ( test -z "$sandbox_arg" && \
       test "x$have_seccomp_filter" = "x1" && \
       test "x$ac_cv_header_elf_h" = "xyes" && \
       test "x$ac_cv_header_linux_audit_h" = "xyes" && \
       test "x$ac_cv_header_linux_filter_h" = "xyes" && \
       test "x$seccomp_audit_arch" != "x" && \
       test "x$have_linux_no_new_privs" = "x1" && \
       test "x$ac_cv_func_prctl" = "xyes" ) ; then
	test "x$seccomp_audit_arch" = "x" && \
		AC_MSG_ERROR([seccomp_filter sandbox not supported on $host])
	test "x$have_linux_no_new_privs" != "x1" && \
		AC_MSG_ERROR([seccomp_filter sandbox requires PR_SET_NO_NEW_PRIVS])
	test "x$have_seccomp_filter" != "x1" && \
		AC_MSG_ERROR([seccomp_filter sandbox requires seccomp headers])
	test "x$ac_cv_func_prctl" != "xyes" && \
		AC_MSG_ERROR([seccomp_filter sandbox requires prctl function])
	SANDBOX_STYLE="seccomp_filter"
	AC_DEFINE([SANDBOX_SECCOMP_FILTER], [1], [Sandbox using seccomp filter])
elif test "x$sandbox_arg" = "xcapsicum" || \
     ( test -z "$sandbox_arg" && \
       test "x$ac_cv_header_sys_capsicum_h" = "xyes" && \
       test "x$ac_cv_func_cap_rights_limit" = "xyes") ; then
       test "x$ac_cv_header_sys_capsicum_h" != "xyes" && \
		AC_MSG_ERROR([capsicum sandbox requires sys/capsicum.h header])
       test "x$ac_cv_func_cap_rights_limit" != "xyes" && \
		AC_MSG_ERROR([capsicum sandbox requires cap_rights_limit function])
       SANDBOX_STYLE="capsicum"
       AC_DEFINE([SANDBOX_CAPSICUM], [1], [Sandbox using capsicum])
elif test "x$sandbox_arg" = "xrlimit" || \
     ( test -z "$sandbox_arg" && test "x$ac_cv_func_setrlimit" = "xyes" && \
       test "x$select_works_with_rlimit" = "xyes" && \
       test "x$rlimit_nofile_zero_works" = "xyes" ) ; then
	test "x$ac_cv_func_setrlimit" != "xyes" && \
		AC_MSG_ERROR([rlimit sandbox requires setrlimit function])
	test "x$select_works_with_rlimit" != "xyes" && \
		AC_MSG_ERROR([rlimit sandbox requires select to work with rlimit])
	SANDBOX_STYLE="rlimit"
	AC_DEFINE([SANDBOX_RLIMIT], [1], [Sandbox using setrlimit(2)])
elif test "x$sandbox_arg" = "xsolaris" || \
   ( test -z "$sandbox_arg" && test "x$SOLARIS_PRIVS" = "xyes" ) ; then
	SANDBOX_STYLE="solaris"
	AC_DEFINE([SANDBOX_SOLARIS], [1], [Sandbox using Solaris/Illumos privileges])
elif test -z "$sandbox_arg" || test "x$sandbox_arg" = "xno" || \
     test "x$sandbox_arg" = "xnone" || test "x$sandbox_arg" = "xnull" ; then
	SANDBOX_STYLE="none"
	AC_DEFINE([SANDBOX_NULL], [1], [no privsep sandboxing])
else
	AC_MSG_ERROR([unsupported --with-sandbox])
fi

# Cheap hack to ensure NEWS-OS libraries are arranged right.
if test ! -z "$SONY" ; then
  LIBS="$LIBS -liberty";
fi

# Check for  long long datatypes
AC_CHECK_TYPES([long long, unsigned long long, long double])

# Check datatype sizes
AC_CHECK_SIZEOF([short int], [2])
AC_CHECK_SIZEOF([int], [4])
AC_CHECK_SIZEOF([long int], [4])
AC_CHECK_SIZEOF([long long int], [8])

# Sanity check long long for some platforms (AIX)
if test "x$ac_cv_sizeof_long_long_int" = "x4" ; then
	ac_cv_sizeof_long_long_int=0
fi

# compute LLONG_MIN and LLONG_MAX if we don't know them.
if test -z "$have_llong_max"; then
	AC_MSG_CHECKING([for max value of long long])
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[
#include <stdio.h>
/* Why is this so damn hard? */
#ifdef __GNUC__
# undef __GNUC__
#endif
#define __USE_ISOC99
#include <limits.h>
#define DATA "conftest.llminmax"
#define my_abs(a) ((a) < 0 ? ((a) * -1) : (a))

/*
 * printf in libc on some platforms (eg old Tru64) does not understand %lld so
 * we do this the hard way.
 */
static int
fprint_ll(FILE *f, long long n)
{
	unsigned int i;
	int l[sizeof(long long) * 8];

	if (n < 0)
		if (fprintf(f, "-") < 0)
			return -1;
	for (i = 0; n != 0; i++) {
		l[i] = my_abs(n % 10);
		n /= 10;
	}
	do {
		if (fprintf(f, "%d", l[--i]) < 0)
			return -1;
	} while (i != 0);
	if (fprintf(f, " ") < 0)
		return -1;
	return 0;
}
		]], [[
	FILE *f;
	long long i, llmin, llmax = 0;

	if((f = fopen(DATA,"w")) == NULL)
		exit(1);

#if defined(LLONG_MIN) && defined(LLONG_MAX)
	fprintf(stderr, "Using system header for LLONG_MIN and LLONG_MAX\n");
	llmin = LLONG_MIN;
	llmax = LLONG_MAX;
#else
	fprintf(stderr, "Calculating  LLONG_MIN and LLONG_MAX\n");
	/* This will work on one's complement and two's complement */
	for (i = 1; i > llmax; i <<= 1, i++)
		llmax = i;
	llmin = llmax + 1LL;	/* wrap */
#endif

	/* Sanity check */
	if (llmin + 1 < llmin || llmin - 1 < llmin || llmax + 1 > llmax
	    || llmax - 1 > llmax || llmin == llmax || llmin == 0
	    || llmax == 0 || llmax < LONG_MAX || llmin > LONG_MIN) {
		fprintf(f, "unknown unknown\n");
		exit(2);
	}

	if (fprint_ll(f, llmin) < 0)
		exit(3);
	if (fprint_ll(f, llmax) < 0)
		exit(4);
	if (fclose(f) < 0)
		exit(5);
	exit(0);
		]])],
		[
			llong_min=`$AWK '{print $1}' conftest.llminmax`
			llong_max=`$AWK '{print $2}' conftest.llminmax`

			AC_MSG_RESULT([$llong_max])
			AC_DEFINE_UNQUOTED([LLONG_MAX], [${llong_max}LL],
			    [max value of long long calculated by configure])
			AC_MSG_CHECKING([for min value of long long])
			AC_MSG_RESULT([$llong_min])
			AC_DEFINE_UNQUOTED([LLONG_MIN], [${llong_min}LL],
			    [min value of long long calculated by configure])
		],
		[
			AC_MSG_RESULT([not found])
		],
		[
			AC_MSG_WARN([cross compiling: not checking])
		]
	)
fi


# More checks for data types
AC_CACHE_CHECK([for u_int type], ac_cv_have_u_int, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <sys/types.h> ]],
	[[ u_int a; a = 1;]])],
	[ ac_cv_have_u_int="yes" ], [ ac_cv_have_u_int="no"
	])
])
if test "x$ac_cv_have_u_int" = "xyes" ; then
	AC_DEFINE([HAVE_U_INT], [1], [define if you have u_int data type])
	have_u_int=1
fi

AC_CACHE_CHECK([for intXX_t types], ac_cv_have_intxx_t, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <sys/types.h> ]],
	[[ int8_t a; int16_t b; int32_t c; a = b = c = 1;]])],
	[ ac_cv_have_intxx_t="yes" ], [ ac_cv_have_intxx_t="no"
	])
])
if test "x$ac_cv_have_intxx_t" = "xyes" ; then
	AC_DEFINE([HAVE_INTXX_T], [1], [define if you have intxx_t data type])
	have_intxx_t=1
fi

if (test -z "$have_intxx_t" && \
	   test "x$ac_cv_header_stdint_h" = "xyes")
then
    AC_MSG_CHECKING([for intXX_t types in stdint.h])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <stdint.h> ]],
	[[ int8_t a; int16_t b; int32_t c; a = b = c = 1;]])],
		[
			AC_DEFINE([HAVE_INTXX_T])
			AC_MSG_RESULT([yes])
		], [ AC_MSG_RESULT([no])
	])
fi

AC_CACHE_CHECK([for int64_t type], ac_cv_have_int64_t, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif
#include <sys/socket.h>
#ifdef HAVE_SYS_BITYPES_H
# include <sys/bitypes.h>
#endif
		]], [[
int64_t a; a = 1;
		]])],
	[ ac_cv_have_int64_t="yes" ], [ ac_cv_have_int64_t="no"
	])
])
if test "x$ac_cv_have_int64_t" = "xyes" ; then
	AC_DEFINE([HAVE_INT64_T], [1], [define if you have int64_t data type])
fi

AC_CACHE_CHECK([for u_intXX_t types], ac_cv_have_u_intxx_t, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <sys/types.h> ]],
	[[ u_int8_t a; u_int16_t b; u_int32_t c; a = b = c = 1;]])],
	[ ac_cv_have_u_intxx_t="yes" ], [ ac_cv_have_u_intxx_t="no"
	])
])
if test "x$ac_cv_have_u_intxx_t" = "xyes" ; then
	AC_DEFINE([HAVE_U_INTXX_T], [1], [define if you have u_intxx_t data type])
	have_u_intxx_t=1
fi

if test -z "$have_u_intxx_t" ; then
    AC_MSG_CHECKING([for u_intXX_t types in sys/socket.h])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <sys/socket.h> ]],
	[[ u_int8_t a; u_int16_t b; u_int32_t c; a = b = c = 1;]])],
		[
			AC_DEFINE([HAVE_U_INTXX_T])
			AC_MSG_RESULT([yes])
		], [ AC_MSG_RESULT([no])
	])
fi

AC_CACHE_CHECK([for u_int64_t types], ac_cv_have_u_int64_t, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <sys/types.h> ]],
	[[ u_int64_t a; a = 1;]])],
	[ ac_cv_have_u_int64_t="yes" ], [ ac_cv_have_u_int64_t="no"
	])
])
if test "x$ac_cv_have_u_int64_t" = "xyes" ; then
	AC_DEFINE([HAVE_U_INT64_T], [1], [define if you have u_int64_t data type])
	have_u_int64_t=1
fi

if (test -z "$have_u_int64_t" && \
	   test "x$ac_cv_header_sys_bitypes_h" = "xyes")
then
    AC_MSG_CHECKING([for u_int64_t type in sys/bitypes.h])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <sys/bitypes.h> ]],
	[[ u_int64_t a; a = 1]])],
		[
			AC_DEFINE([HAVE_U_INT64_T])
			AC_MSG_RESULT([yes])
		], [ AC_MSG_RESULT([no])
	])
fi

if test -z "$have_u_intxx_t" ; then
	AC_CACHE_CHECK([for uintXX_t types], ac_cv_have_uintxx_t, [
		AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
			]], [[
	uint8_t a;
	uint16_t b;
	uint32_t c;
	a = b = c = 1;
			]])],
		[ ac_cv_have_uintxx_t="yes" ], [ ac_cv_have_uintxx_t="no"
		])
	])
	if test "x$ac_cv_have_uintxx_t" = "xyes" ; then
		AC_DEFINE([HAVE_UINTXX_T], [1],
			[define if you have uintxx_t data type])
	fi
fi

if (test -z "$have_uintxx_t" && \
	   test "x$ac_cv_header_stdint_h" = "xyes")
then
    AC_MSG_CHECKING([for uintXX_t types in stdint.h])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <stdint.h> ]],
	[[ uint8_t a; uint16_t b; uint32_t c; a = b = c = 1;]])],
		[
			AC_DEFINE([HAVE_UINTXX_T])
			AC_MSG_RESULT([yes])
		], [ AC_MSG_RESULT([no])
	])
fi

if (test -z "$have_uintxx_t" && \
	   test "x$ac_cv_header_inttypes_h" = "xyes")
then
    AC_MSG_CHECKING([for uintXX_t types in inttypes.h])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <inttypes.h> ]],
	[[ uint8_t a; uint16_t b; uint32_t c; a = b = c = 1;]])],
		[
			AC_DEFINE([HAVE_UINTXX_T])
			AC_MSG_RESULT([yes])
		], [ AC_MSG_RESULT([no])
	])
fi

if (test -z "$have_u_intxx_t" || test -z "$have_intxx_t" && \
	   test "x$ac_cv_header_sys_bitypes_h" = "xyes")
then
	AC_MSG_CHECKING([for intXX_t and u_intXX_t types in sys/bitypes.h])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/bitypes.h>
		]], [[
			int8_t a; int16_t b; int32_t c;
			u_int8_t e; u_int16_t f; u_int32_t g;
			a = b = c = e = f = g = 1;
		]])],
		[
			AC_DEFINE([HAVE_U_INTXX_T])
			AC_DEFINE([HAVE_INTXX_T])
			AC_MSG_RESULT([yes])
		], [AC_MSG_RESULT([no])
	])
fi


AC_CACHE_CHECK([for u_char], ac_cv_have_u_char, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <sys/types.h> ]],
	[[ u_char foo; foo = 125; ]])],
	[ ac_cv_have_u_char="yes" ], [ ac_cv_have_u_char="no"
	])
])
if test "x$ac_cv_have_u_char" = "xyes" ; then
	AC_DEFINE([HAVE_U_CHAR], [1], [define if you have u_char data type])
fi

AC_CHECK_TYPES([intmax_t, uintmax_t], , , [
#include <sys/types.h>
#include <stdint.h>
])

TYPE_SOCKLEN_T

AC_CHECK_TYPES([sig_atomic_t], , , [#include <signal.h>])
AC_CHECK_TYPES([fsblkcnt_t, fsfilcnt_t], , , [
#include <sys/types.h>
#ifdef HAVE_SYS_BITYPES_H
#include <sys/bitypes.h>
#endif
#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif
#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif
])

AC_CHECK_MEMBERS([struct statfs.f_flags], [], [], [[
#include <sys/types.h>
#ifdef HAVE_SYS_BITYPES_H
#include <sys/bitypes.h>
#endif
#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif
#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif
#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#endif
]])


AC_CHECK_TYPES([in_addr_t, in_port_t], , ,
[#include <sys/types.h>
#include <netinet/in.h>])

AC_CACHE_CHECK([for size_t], ac_cv_have_size_t, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <sys/types.h> ]],
	[[ size_t foo; foo = 1235; ]])],
	[ ac_cv_have_size_t="yes" ], [ ac_cv_have_size_t="no"
	])
])
if test "x$ac_cv_have_size_t" = "xyes" ; then
	AC_DEFINE([HAVE_SIZE_T], [1], [define if you have size_t data type])
fi

AC_CACHE_CHECK([for ssize_t], ac_cv_have_ssize_t, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <sys/types.h> ]],
	[[ ssize_t foo; foo = 1235; ]])],
	[ ac_cv_have_ssize_t="yes" ], [ ac_cv_have_ssize_t="no"
	])
])
if test "x$ac_cv_have_ssize_t" = "xyes" ; then
	AC_DEFINE([HAVE_SSIZE_T], [1], [define if you have ssize_t data type])
fi

AC_CACHE_CHECK([for clock_t], ac_cv_have_clock_t, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <time.h> ]],
	[[ clock_t foo; foo = 1235; ]])],
	[ ac_cv_have_clock_t="yes" ], [ ac_cv_have_clock_t="no"
	])
])
if test "x$ac_cv_have_clock_t" = "xyes" ; then
	AC_DEFINE([HAVE_CLOCK_T], [1], [define if you have clock_t data type])
fi

AC_CACHE_CHECK([for sa_family_t], ac_cv_have_sa_family_t, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
		]], [[ sa_family_t foo; foo = 1235; ]])],
	[ ac_cv_have_sa_family_t="yes" ],
	[ AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
		]], [[ sa_family_t foo; foo = 1235; ]])],
		[ ac_cv_have_sa_family_t="yes" ],
		[ ac_cv_have_sa_family_t="no" ]
	)
	])
])
if test "x$ac_cv_have_sa_family_t" = "xyes" ; then
	AC_DEFINE([HAVE_SA_FAMILY_T], [1],
		[define if you have sa_family_t data type])
fi

AC_CACHE_CHECK([for pid_t], ac_cv_have_pid_t, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <sys/types.h> ]],
	[[ pid_t foo; foo = 1235; ]])],
	[ ac_cv_have_pid_t="yes" ], [ ac_cv_have_pid_t="no"
	])
])
if test "x$ac_cv_have_pid_t" = "xyes" ; then
	AC_DEFINE([HAVE_PID_T], [1], [define if you have pid_t data type])
fi

AC_CACHE_CHECK([for mode_t], ac_cv_have_mode_t, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <sys/types.h> ]],
	[[ mode_t foo; foo = 1235; ]])],
	[ ac_cv_have_mode_t="yes" ], [ ac_cv_have_mode_t="no"
	])
])
if test "x$ac_cv_have_mode_t" = "xyes" ; then
	AC_DEFINE([HAVE_MODE_T], [1], [define if you have mode_t data type])
fi


AC_CACHE_CHECK([for struct sockaddr_storage], ac_cv_have_struct_sockaddr_storage, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
		]], [[ struct sockaddr_storage s; ]])],
	[ ac_cv_have_struct_sockaddr_storage="yes" ],
	[ ac_cv_have_struct_sockaddr_storage="no"
	])
])
if test "x$ac_cv_have_struct_sockaddr_storage" = "xyes" ; then
	AC_DEFINE([HAVE_STRUCT_SOCKADDR_STORAGE], [1],
		[define if you have struct sockaddr_storage data type])
fi

AC_CACHE_CHECK([for struct sockaddr_in6], ac_cv_have_struct_sockaddr_in6, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <netinet/in.h>
		]], [[ struct sockaddr_in6 s; s.sin6_family = 0; ]])],
	[ ac_cv_have_struct_sockaddr_in6="yes" ],
	[ ac_cv_have_struct_sockaddr_in6="no"
	])
])
if test "x$ac_cv_have_struct_sockaddr_in6" = "xyes" ; then
	AC_DEFINE([HAVE_STRUCT_SOCKADDR_IN6], [1],
		[define if you have struct sockaddr_in6 data type])
fi

AC_CACHE_CHECK([for struct in6_addr], ac_cv_have_struct_in6_addr, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <netinet/in.h>
		]], [[ struct in6_addr s; s.s6_addr[0] = 0; ]])],
	[ ac_cv_have_struct_in6_addr="yes" ],
	[ ac_cv_have_struct_in6_addr="no"
	])
])
if test "x$ac_cv_have_struct_in6_addr" = "xyes" ; then
	AC_DEFINE([HAVE_STRUCT_IN6_ADDR], [1],
		[define if you have struct in6_addr data type])

dnl Now check for sin6_scope_id
	AC_CHECK_MEMBERS([struct sockaddr_in6.sin6_scope_id], , ,
		[
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#include <netinet/in.h>
		])
fi

AC_CACHE_CHECK([for struct addrinfo], ac_cv_have_struct_addrinfo, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
		]], [[ struct addrinfo s; s.ai_flags = AI_PASSIVE; ]])],
	[ ac_cv_have_struct_addrinfo="yes" ],
	[ ac_cv_have_struct_addrinfo="no"
	])
])
if test "x$ac_cv_have_struct_addrinfo" = "xyes" ; then
	AC_DEFINE([HAVE_STRUCT_ADDRINFO], [1],
		[define if you have struct addrinfo data type])
fi

AC_CACHE_CHECK([for struct timeval], ac_cv_have_struct_timeval, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <sys/time.h> ]],
	[[ struct timeval tv; tv.tv_sec = 1;]])],
	[ ac_cv_have_struct_timeval="yes" ],
	[ ac_cv_have_struct_timeval="no"
	])
])
if test "x$ac_cv_have_struct_timeval" = "xyes" ; then
	AC_DEFINE([HAVE_STRUCT_TIMEVAL], [1], [define if you have struct timeval])
	have_struct_timeval=1
fi

AC_CHECK_TYPES([struct timespec])

# We need int64_t or else certain parts of the compile will fail.
if test "x$ac_cv_have_int64_t" = "xno" && \
	test "x$ac_cv_sizeof_long_int" != "x8" && \
	test "x$ac_cv_sizeof_long_long_int" = "x0" ; then
	echo "OpenSSH requires int64_t support.  Contact your vendor or install"
	echo "an alternative compiler (I.E., GCC) before continuing."
	echo ""
	exit 1;
else
dnl test snprintf (broken on SCO w/gcc)
	AC_RUN_IFELSE(
		[AC_LANG_SOURCE([[
#include <stdio.h>
#include <string.h>
#ifdef HAVE_SNPRINTF
main()
{
	char buf[50];
	char expected_out[50];
	int mazsize = 50 ;
#if (SIZEOF_LONG_INT == 8)
	long int num = 0x7fffffffffffffff;
#else
	long long num = 0x7fffffffffffffffll;
#endif
	strcpy(expected_out, "9223372036854775807");
	snprintf(buf, mazsize, "%lld", num);
	if(strcmp(buf, expected_out) != 0)
		exit(1);
	exit(0);
}
#else
main() { exit(0); }
#endif
		]])], [ true ], [ AC_DEFINE([BROKEN_SNPRINTF]) ],
		AC_MSG_WARN([cross compiling: Assuming working snprintf()])
	)
fi

dnl Checks for structure members
OSSH_CHECK_HEADER_FOR_FIELD([ut_host], [utmp.h], [HAVE_HOST_IN_UTMP])
OSSH_CHECK_HEADER_FOR_FIELD([ut_host], [utmpx.h], [HAVE_HOST_IN_UTMPX])
OSSH_CHECK_HEADER_FOR_FIELD([syslen], [utmpx.h], [HAVE_SYSLEN_IN_UTMPX])
OSSH_CHECK_HEADER_FOR_FIELD([ut_pid], [utmp.h], [HAVE_PID_IN_UTMP])
OSSH_CHECK_HEADER_FOR_FIELD([ut_type], [utmp.h], [HAVE_TYPE_IN_UTMP])
OSSH_CHECK_HEADER_FOR_FIELD([ut_type], [utmpx.h], [HAVE_TYPE_IN_UTMPX])
OSSH_CHECK_HEADER_FOR_FIELD([ut_tv], [utmp.h], [HAVE_TV_IN_UTMP])
OSSH_CHECK_HEADER_FOR_FIELD([ut_id], [utmp.h], [HAVE_ID_IN_UTMP])
OSSH_CHECK_HEADER_FOR_FIELD([ut_id], [utmpx.h], [HAVE_ID_IN_UTMPX])
OSSH_CHECK_HEADER_FOR_FIELD([ut_addr], [utmp.h], [HAVE_ADDR_IN_UTMP])
OSSH_CHECK_HEADER_FOR_FIELD([ut_addr], [utmpx.h], [HAVE_ADDR_IN_UTMPX])
OSSH_CHECK_HEADER_FOR_FIELD([ut_addr_v6], [utmp.h], [HAVE_ADDR_V6_IN_UTMP])
OSSH_CHECK_HEADER_FOR_FIELD([ut_addr_v6], [utmpx.h], [HAVE_ADDR_V6_IN_UTMPX])
OSSH_CHECK_HEADER_FOR_FIELD([ut_exit], [utmp.h], [HAVE_EXIT_IN_UTMP])
OSSH_CHECK_HEADER_FOR_FIELD([ut_time], [utmp.h], [HAVE_TIME_IN_UTMP])
OSSH_CHECK_HEADER_FOR_FIELD([ut_time], [utmpx.h], [HAVE_TIME_IN_UTMPX])
OSSH_CHECK_HEADER_FOR_FIELD([ut_tv], [utmpx.h], [HAVE_TV_IN_UTMPX])

AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_CHECK_MEMBERS([struct stat.st_mtim])
AC_CHECK_MEMBERS([struct stat.st_mtime])
AC_CHECK_MEMBERS([struct passwd.pw_gecos, struct passwd.pw_class,
struct passwd.pw_change, struct passwd.pw_expire],
[], [], [[
#include <sys/types.h>
#include <pwd.h>
]])

AC_CHECK_MEMBER([struct __res_state.retrans], [], [AC_DEFINE([__res_state], [state],
	[Define if we don't have struct __res_state in resolv.h])],
[[
#include <stdio.h>
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
]])

AC_CACHE_CHECK([for ss_family field in struct sockaddr_storage],
		ac_cv_have_ss_family_in_struct_ss, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
		]], [[ struct sockaddr_storage s; s.ss_family = 1; ]])],
	[ ac_cv_have_ss_family_in_struct_ss="yes" ],
	[ ac_cv_have_ss_family_in_struct_ss="no" ])
])
if test "x$ac_cv_have_ss_family_in_struct_ss" = "xyes" ; then
	AC_DEFINE([HAVE_SS_FAMILY_IN_SS], [1], [Fields in struct sockaddr_storage])
fi

AC_CACHE_CHECK([for __ss_family field in struct sockaddr_storage],
		ac_cv_have___ss_family_in_struct_ss, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
		]], [[ struct sockaddr_storage s; s.__ss_family = 1; ]])],
	[ ac_cv_have___ss_family_in_struct_ss="yes" ],
	[ ac_cv_have___ss_family_in_struct_ss="no"
	])
])
if test "x$ac_cv_have___ss_family_in_struct_ss" = "xyes" ; then
	AC_DEFINE([HAVE___SS_FAMILY_IN_SS], [1],
		[Fields in struct sockaddr_storage])
fi

dnl make sure we're using the real structure members and not defines
AC_CACHE_CHECK([for msg_accrights field in struct msghdr],
		ac_cv_have_accrights_in_msghdr, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
		]], [[
#ifdef msg_accrights
#error "msg_accrights is a macro"
exit(1);
#endif
struct msghdr m;
m.msg_accrights = 0;
exit(0);
		]])],
		[ ac_cv_have_accrights_in_msghdr="yes" ],
		[ ac_cv_have_accrights_in_msghdr="no" ]
	)
])
if test "x$ac_cv_have_accrights_in_msghdr" = "xyes" ; then
	AC_DEFINE([HAVE_ACCRIGHTS_IN_MSGHDR], [1],
		[Define if your system uses access rights style
		file descriptor passing])
fi

AC_MSG_CHECKING([if struct statvfs.f_fsid is integral type])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/param.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif
#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif
	]], [[ struct statvfs s; s.f_fsid = 0; ]])],
	[ AC_MSG_RESULT([yes]) ],
	[ AC_MSG_RESULT([no])

	AC_MSG_CHECKING([if fsid_t has member val])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/statvfs.h>
	]], [[ fsid_t t; t.val[0] = 0; ]])],
	[ AC_MSG_RESULT([yes])
	  AC_DEFINE([FSID_HAS_VAL], [1], [fsid_t has member val]) ],
	[ AC_MSG_RESULT([no]) ])

	AC_MSG_CHECKING([if f_fsid has member __val])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/statvfs.h>
	]], [[ fsid_t t; t.__val[0] = 0; ]])],
	[ AC_MSG_RESULT([yes])
	  AC_DEFINE([FSID_HAS___VAL], [1], [fsid_t has member __val]) ],
	[ AC_MSG_RESULT([no]) ])
])

AC_CACHE_CHECK([for msg_control field in struct msghdr],
		ac_cv_have_control_in_msghdr, [
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
		]], [[
#ifdef msg_control
#error "msg_control is a macro"
exit(1);
#endif
struct msghdr m;
m.msg_control = 0;
exit(0);
		]])],
		[ ac_cv_have_control_in_msghdr="yes" ],
		[ ac_cv_have_control_in_msghdr="no" ]
	)
])
if test "x$ac_cv_have_control_in_msghdr" = "xyes" ; then
	AC_DEFINE([HAVE_CONTROL_IN_MSGHDR], [1],
		[Define if your system uses ancillary data style
		file descriptor passing])
fi

AC_CACHE_CHECK([if libc defines __progname], ac_cv_libc_defines___progname, [
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[]],
		[[ extern char *__progname; printf("%s", __progname); ]])],
	[ ac_cv_libc_defines___progname="yes" ],
	[ ac_cv_libc_defines___progname="no"
	])
])
if test "x$ac_cv_libc_defines___progname" = "xyes" ; then
	AC_DEFINE([HAVE___PROGNAME], [1], [Define if libc defines __progname])
fi

AC_CACHE_CHECK([whether $CC implements __FUNCTION__], ac_cv_cc_implements___FUNCTION__, [
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[ #include <stdio.h> ]],
		[[ printf("%s", __FUNCTION__); ]])],
	[ ac_cv_cc_implements___FUNCTION__="yes" ],
	[ ac_cv_cc_implements___FUNCTION__="no"
	])
])
if test "x$ac_cv_cc_implements___FUNCTION__" = "xyes" ; then
	AC_DEFINE([HAVE___FUNCTION__], [1],
		[Define if compiler implements __FUNCTION__])
fi

AC_CACHE_CHECK([whether $CC implements __func__], ac_cv_cc_implements___func__, [
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[ #include <stdio.h> ]],
		[[ printf("%s", __func__); ]])],
	[ ac_cv_cc_implements___func__="yes" ],
	[ ac_cv_cc_implements___func__="no"
	])
])
if test "x$ac_cv_cc_implements___func__" = "xyes" ; then
	AC_DEFINE([HAVE___func__], [1], [Define if compiler implements __func__])
fi

AC_CACHE_CHECK([whether va_copy exists], ac_cv_have_va_copy, [
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <stdarg.h>
va_list x,y;
		]], [[ va_copy(x,y); ]])],
	[ ac_cv_have_va_copy="yes" ],
	[ ac_cv_have_va_copy="no"
	])
])
if test "x$ac_cv_have_va_copy" = "xyes" ; then
	AC_DEFINE([HAVE_VA_COPY], [1], [Define if va_copy exists])
fi

AC_CACHE_CHECK([whether __va_copy exists], ac_cv_have___va_copy, [
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <stdarg.h>
va_list x,y;
		]], [[ __va_copy(x,y); ]])],
	[ ac_cv_have___va_copy="yes" ], [ ac_cv_have___va_copy="no"
	])
])
if test "x$ac_cv_have___va_copy" = "xyes" ; then
	AC_DEFINE([HAVE___VA_COPY], [1], [Define if __va_copy exists])
fi

AC_CACHE_CHECK([whether getopt has optreset support],
		ac_cv_have_getopt_optreset, [
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[ #include <getopt.h> ]],
		[[ extern int optreset; optreset = 0; ]])],
	[ ac_cv_have_getopt_optreset="yes" ],
	[ ac_cv_have_getopt_optreset="no"
	])
])
if test "x$ac_cv_have_getopt_optreset" = "xyes" ; then
	AC_DEFINE([HAVE_GETOPT_OPTRESET], [1],
		[Define if your getopt(3) defines and uses optreset])
fi

AC_CACHE_CHECK([if libc defines sys_errlist], ac_cv_libc_defines_sys_errlist, [
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[]],
[[ extern const char *const sys_errlist[]; printf("%s", sys_errlist[0]);]])],
	[ ac_cv_libc_defines_sys_errlist="yes" ],
	[ ac_cv_libc_defines_sys_errlist="no"
	])
])
if test "x$ac_cv_libc_defines_sys_errlist" = "xyes" ; then
	AC_DEFINE([HAVE_SYS_ERRLIST], [1],
		[Define if your system defines sys_errlist[]])
fi


AC_CACHE_CHECK([if libc defines sys_nerr], ac_cv_libc_defines_sys_nerr, [
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[]],
[[ extern int sys_nerr; printf("%i", sys_nerr);]])],
	[ ac_cv_libc_defines_sys_nerr="yes" ],
	[ ac_cv_libc_defines_sys_nerr="no"
	])
])
if test "x$ac_cv_libc_defines_sys_nerr" = "xyes" ; then
	AC_DEFINE([HAVE_SYS_NERR], [1], [Define if your system defines sys_nerr])
fi

# Check libraries needed by DNS fingerprint support
AC_SEARCH_LIBS([getrrsetbyname], [resolv],
	[AC_DEFINE([HAVE_GETRRSETBYNAME], [1],
		[Define if getrrsetbyname() exists])],
	[
		# Needed by our getrrsetbyname()
		AC_SEARCH_LIBS([res_query], [resolv])
		AC_SEARCH_LIBS([dn_expand], [resolv])
		AC_MSG_CHECKING([if res_query will link])
		AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <resolv.h>
				]], [[
	res_query (0, 0, 0, 0, 0);
				]])],
		    AC_MSG_RESULT([yes]),
		   [AC_MSG_RESULT([no])
		    saved_LIBS="$LIBS"
		    LIBS="$LIBS -lresolv"
		    AC_MSG_CHECKING([for res_query in -lresolv])
		    AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <resolv.h>
				]], [[
	res_query (0, 0, 0, 0, 0);
				]])],
			[AC_MSG_RESULT([yes])],
			[LIBS="$saved_LIBS"
			 AC_MSG_RESULT([no])])
		    ])
		AC_CHECK_FUNCS([_getshort _getlong])
		AC_CHECK_DECLS([_getshort, _getlong], , ,
		    [#include <sys/types.h>
		    #include <arpa/nameser.h>])
		AC_CHECK_MEMBER([HEADER.ad],
			[AC_DEFINE([HAVE_HEADER_AD], [1],
			    [Define if HEADER.ad exists in arpa/nameser.h])], ,
			[#include <arpa/nameser.h>])
	])

AC_MSG_CHECKING([if struct __res_state _res is an extern])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <stdio.h>
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
extern struct __res_state _res;
		]], [[
struct __res_state *volatile p = &_res;  /* force resolution of _res */
return 0;
		]],)],
		[AC_MSG_RESULT([yes])
		 AC_DEFINE([HAVE__RES_EXTERN], [1],
		    [Define if you have struct __res_state _res as an extern])
		],
		[ AC_MSG_RESULT([no]) ]
)

# Check whether user wants SELinux support
SELINUX_MSG="no"
LIBSELINUX=""
AC_ARG_WITH([selinux],
	[  --with-selinux          Enable SELinux support],
	[ if test "x$withval" != "xno" ; then
		save_LIBS="$LIBS"
		AC_DEFINE([WITH_SELINUX], [1],
			[Define if you want SELinux support.])
		SELINUX_MSG="yes"
		AC_CHECK_HEADER([selinux/selinux.h], ,
			AC_MSG_ERROR([SELinux support requires selinux.h header]))
		AC_CHECK_LIB([selinux], [setexeccon],
			[ LIBSELINUX="-lselinux"
			  LIBS="$LIBS -lselinux"
			],
			AC_MSG_ERROR([SELinux support requires libselinux library]))
		SSHLIBS="$SSHLIBS $LIBSELINUX"
		SSHDLIBS="$SSHDLIBS $LIBSELINUX"
		AC_CHECK_FUNCS([getseuserbyname get_default_context_with_level])
		LIBS="$save_LIBS"
	fi ]
)
AC_SUBST([SSHLIBS])
AC_SUBST([SSHDLIBS])

# Check whether user wants Kerberos 5 support
KRB5_MSG="no"
AC_ARG_WITH([kerberos5],
	[  --with-kerberos5=PATH   Enable Kerberos 5 support],
	[ if test "x$withval" != "xno" ; then
		if test "x$withval" = "xyes" ; then
			KRB5ROOT="/usr/local"
		else
			KRB5ROOT=${withval}
		fi

		AC_DEFINE([KRB5], [1], [Define if you want Kerberos 5 support])
		KRB5_MSG="yes"

		AC_PATH_TOOL([KRB5CONF], [krb5-config],
			     [$KRB5ROOT/bin/krb5-config],
			     [$KRB5ROOT/bin:$PATH])
		if test -x $KRB5CONF ; then
			K5CFLAGS="`$KRB5CONF --cflags`"
			K5LIBS="`$KRB5CONF --libs`"
			CPPFLAGS="$CPPFLAGS $K5CFLAGS"

			AC_MSG_CHECKING([for gssapi support])
			if $KRB5CONF | grep gssapi >/dev/null ; then
				AC_MSG_RESULT([yes])
				AC_DEFINE([GSSAPI], [1],
					[Define this if you want GSSAPI
					support in the version 2 protocol])
				GSSCFLAGS="`$KRB5CONF --cflags gssapi`"
				GSSLIBS="`$KRB5CONF --libs gssapi`"
				CPPFLAGS="$CPPFLAGS $GSSCFLAGS"
			else
				AC_MSG_RESULT([no])
			fi
			AC_MSG_CHECKING([whether we are using Heimdal])
			AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <krb5.h>
				]], [[ char *tmp = heimdal_version; ]])],
				[ AC_MSG_RESULT([yes])
				AC_DEFINE([HEIMDAL], [1],
				[Define this if you are using the Heimdal
				version of Kerberos V5]) ],
				[AC_MSG_RESULT([no])
			])
		else
			CPPFLAGS="$CPPFLAGS -I${KRB5ROOT}/include"
			LDFLAGS="$LDFLAGS -L${KRB5ROOT}/lib"
			AC_MSG_CHECKING([whether we are using Heimdal])
			AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <krb5.h>
				]], [[ char *tmp = heimdal_version; ]])],
					[ AC_MSG_RESULT([yes])
					 AC_DEFINE([HEIMDAL])
					 K5LIBS="-lkrb5"
					 K5LIBS="$K5LIBS -lcom_err -lasn1"
					 AC_CHECK_LIB([roken], [net_write],
					   [K5LIBS="$K5LIBS -lroken"])
					 AC_CHECK_LIB([des], [des_cbc_encrypt],
					   [K5LIBS="$K5LIBS -ldes"])
				       ], [ AC_MSG_RESULT([no])
					 K5LIBS="-lkrb5 -lk5crypto -lcom_err"
			])
			AC_SEARCH_LIBS([dn_expand], [resolv])

			AC_CHECK_LIB([gssapi_krb5], [gss_init_sec_context],
				[ AC_DEFINE([GSSAPI])
				  GSSLIBS="-lgssapi_krb5" ],
				[ AC_CHECK_LIB([gssapi], [gss_init_sec_context],
					[ AC_DEFINE([GSSAPI])
					  GSSLIBS="-lgssapi" ],
					[ AC_CHECK_LIB([gss], [gss_init_sec_context],
						[ AC_DEFINE([GSSAPI])
						  GSSLIBS="-lgss" ],
						AC_MSG_WARN([Cannot find any suitable gss-api library - build may fail]))
					])
				])

			AC_CHECK_HEADER([gssapi.h], ,
				[ unset ac_cv_header_gssapi_h
				  CPPFLAGS="$CPPFLAGS -I${KRB5ROOT}/include/gssapi"
				  AC_CHECK_HEADERS([gssapi.h], ,
					AC_MSG_WARN([Cannot find any suitable gss-api header - build may fail])
				  )
				]
			)

			oldCPP="$CPPFLAGS"
			CPPFLAGS="$CPPFLAGS -I${KRB5ROOT}/include/gssapi"
			AC_CHECK_HEADER([gssapi_krb5.h], ,
					[ CPPFLAGS="$oldCPP" ])

		fi
		if test ! -z "$need_dash_r" ; then
			LDFLAGS="$LDFLAGS -R${KRB5ROOT}/lib"
		fi
		if test ! -z "$blibpath" ; then
			blibpath="$blibpath:${KRB5ROOT}/lib"
		fi

		AC_CHECK_HEADERS([gssapi.h gssapi/gssapi.h])
		AC_CHECK_HEADERS([gssapi_krb5.h gssapi/gssapi_krb5.h])
		AC_CHECK_HEADERS([gssapi_generic.h gssapi/gssapi_generic.h])

		AC_SEARCH_LIBS([k_hasafs], [kafs], [AC_DEFINE([USE_AFS], [1],
			[Define this if you want to use libkafs' AFS support])])

		AC_CHECK_DECLS([GSS_C_NT_HOSTBASED_SERVICE], [], [], [[
#ifdef HAVE_GSSAPI_H
# include <gssapi.h>
#elif defined(HAVE_GSSAPI_GSSAPI_H)
# include <gssapi/gssapi.h>
#endif

#ifdef HAVE_GSSAPI_GENERIC_H
# include <gssapi_generic.h>
#elif defined(HAVE_GSSAPI_GSSAPI_GENERIC_H)
# include <gssapi/gssapi_generic.h>
#endif
		]])
		saved_LIBS="$LIBS"
		LIBS="$LIBS $K5LIBS"
		AC_CHECK_FUNCS([krb5_cc_new_unique krb5_get_error_message krb5_free_error_message])
		LIBS="$saved_LIBS"

	fi
	]
)
AC_SUBST([GSSLIBS])
AC_SUBST([K5LIBS])

# Looking for programs, paths and files

PRIVSEP_PATH=/var/empty
AC_ARG_WITH([privsep-path],
	[  --with-privsep-path=xxx Path for privilege separation chroot (default=/var/empty)],
	[
		if test -n "$withval"  &&  test "x$withval" != "xno"  &&  \
		    test "x${withval}" != "xyes"; then
			PRIVSEP_PATH=$withval
		fi
	]
)
AC_SUBST([PRIVSEP_PATH])

AC_ARG_WITH([xauth],
	[  --with-xauth=PATH       Specify path to xauth program ],
	[
		if test -n "$withval"  &&  test "x$withval" != "xno"  &&  \
		    test "x${withval}" != "xyes"; then
			xauth_path=$withval
		fi
	],
	[
		TestPath="$PATH"
		TestPath="${TestPath}${PATH_SEPARATOR}/usr/X/bin"
		TestPath="${TestPath}${PATH_SEPARATOR}/usr/bin/X11"
		TestPath="${TestPath}${PATH_SEPARATOR}/usr/X11R6/bin"
		TestPath="${TestPath}${PATH_SEPARATOR}/usr/openwin/bin"
		AC_PATH_PROG([xauth_path], [xauth], , [$TestPath])
		if (test ! -z "$xauth_path" && test -x "/usr/openwin/bin/xauth") ; then
			xauth_path="/usr/openwin/bin/xauth"
		fi
	]
)

STRIP_OPT=-s
AC_ARG_ENABLE([strip],
	[  --disable-strip         Disable calling strip(1) on install],
	[
		if test "x$enableval" = "xno" ; then
			STRIP_OPT=
		fi
	]
)
AC_SUBST([STRIP_OPT])

if test -z "$xauth_path" ; then
	XAUTH_PATH="undefined"
	AC_SUBST([XAUTH_PATH])
else
	AC_DEFINE_UNQUOTED([XAUTH_PATH], ["$xauth_path"],
		[Define if xauth is found in your path])
	XAUTH_PATH=$xauth_path
	AC_SUBST([XAUTH_PATH])
fi

dnl # --with-maildir=/path/to/mail gets top priority.
dnl # if maildir is set in the platform case statement above we use that.
dnl # Otherwise we run a program to get the dir from system headers.
dnl # We first look for _PATH_MAILDIR then MAILDIR then _PATH_MAIL
dnl # If we find _PATH_MAILDIR we do nothing because that is what
dnl # session.c expects anyway. Otherwise we set to the value found
dnl # stripping any trailing slash. If for some strage reason our program
dnl # does not find what it needs, we default to /var/spool/mail.
# Check for mail directory
AC_ARG_WITH([maildir],
    [  --with-maildir=/path/to/mail    Specify your system mail directory],
    [
	if test "X$withval" != X  &&  test "x$withval" != xno  &&  \
	    test "x${withval}" != xyes; then
		AC_DEFINE_UNQUOTED([MAIL_DIRECTORY], ["$withval"],
            [Set this to your mail directory if you do not have _PATH_MAILDIR])
	    fi
     ],[
	if test "X$maildir" != "X"; then
	    AC_DEFINE_UNQUOTED([MAIL_DIRECTORY], ["$maildir"])
	else
	    AC_MSG_CHECKING([Discovering system mail directory])
	    AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[
#include <stdio.h>
#include <string.h>
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
#ifdef HAVE_MAILLOCK_H
#include <maillock.h>
#endif
#define DATA "conftest.maildir"
	]], [[
	FILE *fd;
	int rc;

	fd = fopen(DATA,"w");
	if(fd == NULL)
		exit(1);

#if defined (_PATH_MAILDIR)
	if ((rc = fprintf(fd ,"_PATH_MAILDIR:%s\n", _PATH_MAILDIR)) <0)
		exit(1);
#elif defined (MAILDIR)
	if ((rc = fprintf(fd ,"MAILDIR:%s\n", MAILDIR)) <0)
		exit(1);
#elif defined (_PATH_MAIL)
	if ((rc = fprintf(fd ,"_PATH_MAIL:%s\n", _PATH_MAIL)) <0)
		exit(1);
#else
	exit (2);
#endif

	exit(0);
		]])],
		[
		    maildir_what=`awk -F: '{print $1}' conftest.maildir`
		    maildir=`awk -F: '{print $2}' conftest.maildir \
			| sed 's|/$||'`
		    AC_MSG_RESULT([Using: $maildir from $maildir_what])
		    if test "x$maildir_what" != "x_PATH_MAILDIR"; then
			AC_DEFINE_UNQUOTED([MAIL_DIRECTORY], ["$maildir"])
		    fi
		],
		[
		    if test "X$ac_status" = "X2";then
# our test program didn't find it. Default to /var/spool/mail
			AC_MSG_RESULT([Using: default value of /var/spool/mail])
			AC_DEFINE_UNQUOTED([MAIL_DIRECTORY], ["/var/spool/mail"])
		     else
			AC_MSG_RESULT([*** not found ***])
		     fi
		],
		[
			AC_MSG_WARN([cross compiling: use --with-maildir=/path/to/mail])
		]
	    )
	fi
    ]
) # maildir

if test ! -z "$cross_compiling" && test "x$cross_compiling" = "xyes"; then
	AC_MSG_WARN([cross compiling: Disabling /dev/ptmx test])
	disable_ptmx_check=yes
fi
if test -z "$no_dev_ptmx" ; then
	if test "x$disable_ptmx_check" != "xyes" ; then
		AC_CHECK_FILE(["/dev/ptmx"],
			[
				AC_DEFINE_UNQUOTED([HAVE_DEV_PTMX], [1],
					[Define if you have /dev/ptmx])
				have_dev_ptmx=1
			]
		)
	fi
fi

if test ! -z "$cross_compiling" && test "x$cross_compiling" != "xyes"; then
	AC_CHECK_FILE(["/dev/ptc"],
		[
			AC_DEFINE_UNQUOTED([HAVE_DEV_PTS_AND_PTC], [1],
				[Define if you have /dev/ptc])
			have_dev_ptc=1
		]
	)
else
	AC_MSG_WARN([cross compiling: Disabling /dev/ptc test])
fi

# Options from here on. Some of these are preset by platform above
AC_ARG_WITH([mantype],
	[  --with-mantype=man|cat|doc  Set man page type],
	[
		case "$withval" in
		man|cat|doc)
			MANTYPE=$withval
			;;
		*)
			AC_MSG_ERROR([invalid man type: $withval])
			;;
		esac
	]
)
if test -z "$MANTYPE"; then
	TestPath="/usr/bin${PATH_SEPARATOR}/usr/ucb"
	AC_PATH_PROGS([NROFF], [nroff awf], [/bin/false], [$TestPath])
	if ${NROFF} -mdoc ${srcdir}/ssh.1 >/dev/null 2>&1; then
		MANTYPE=doc
	elif ${NROFF} -man ${srcdir}/ssh.1 >/dev/null 2>&1; then
		MANTYPE=man
	else
		MANTYPE=cat
	fi
fi
AC_SUBST([MANTYPE])
if test "$MANTYPE" = "doc"; then
	mansubdir=man;
else
	mansubdir=$MANTYPE;
fi
AC_SUBST([mansubdir])

# Check whether to enable MD5 passwords
MD5_MSG="no"
AC_ARG_WITH([md5-passwords],
	[  --with-md5-passwords    Enable use of MD5 passwords],
	[
		if test "x$withval" != "xno" ; then
			AC_DEFINE([HAVE_MD5_PASSWORDS], [1],
				[Define if you want to allow MD5 passwords])
			MD5_MSG="yes"
		fi
	]
)

# Whether to disable shadow password support
AC_ARG_WITH([shadow],
	[  --without-shadow        Disable shadow password support],
	[
		if test "x$withval" = "xno" ; then
			AC_DEFINE([DISABLE_SHADOW])
			disable_shadow=yes
		fi
	]
)

if test -z "$disable_shadow" ; then
	AC_MSG_CHECKING([if the systems has expire shadow information])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <shadow.h>
struct spwd sp;
		]], [[ sp.sp_expire = sp.sp_lstchg = sp.sp_inact = 0; ]])],
		[ sp_expire_available=yes ], [
	])

	if test "x$sp_expire_available" = "xyes" ; then
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAS_SHADOW_EXPIRE], [1],
		    [Define if you want to use shadow password expire field])
	else
		AC_MSG_RESULT([no])
	fi
fi

# Use ip address instead of hostname in $DISPLAY
if test ! -z "$IPADDR_IN_DISPLAY" ; then
	DISPLAY_HACK_MSG="yes"
	AC_DEFINE([IPADDR_IN_DISPLAY], [1],
		[Define if you need to use IP address
		instead of hostname in $DISPLAY])
else
	DISPLAY_HACK_MSG="no"
	AC_ARG_WITH([ipaddr-display],
		[  --with-ipaddr-display   Use ip address instead of hostname in $DISPLAY],
		[
			if test "x$withval" != "xno" ; then
				AC_DEFINE([IPADDR_IN_DISPLAY])
				DISPLAY_HACK_MSG="yes"
			fi
		]
	)
fi

# check for /etc/default/login and use it if present.
AC_ARG_ENABLE([etc-default-login],
	[  --disable-etc-default-login Disable using PATH from /etc/default/login [no]],
	[ if test "x$enableval" = "xno"; then
		AC_MSG_NOTICE([/etc/default/login handling disabled])
		etc_default_login=no
	  else
		etc_default_login=yes
	  fi ],
	[ if test ! -z "$cross_compiling" && test "x$cross_compiling" = "xyes";
	  then
		AC_MSG_WARN([cross compiling: not checking /etc/default/login])
		etc_default_login=no
	  else
		etc_default_login=yes
	  fi ]
)

if test "x$etc_default_login" != "xno"; then
	AC_CHECK_FILE(["/etc/default/login"],
	    [ external_path_file=/etc/default/login ])
	if test "x$external_path_file" = "x/etc/default/login"; then
		AC_DEFINE([HAVE_ETC_DEFAULT_LOGIN], [1],
			[Define if your system has /etc/default/login])
	fi
fi

dnl BSD systems use /etc/login.conf so --with-default-path= has no effect
if test $ac_cv_func_login_getcapbool = "yes" && \
	test $ac_cv_header_login_cap_h = "yes" ; then
	external_path_file=/etc/login.conf
fi

# Whether to mess with the default path
SERVER_PATH_MSG="(default)"
AC_ARG_WITH([default-path],
	[  --with-default-path=    Specify default $PATH environment for server],
	[
		if test "x$external_path_file" = "x/etc/login.conf" ; then
			AC_MSG_WARN([
--with-default-path=PATH has no effect on this system.
Edit /etc/login.conf instead.])
		elif test "x$withval" != "xno" ; then
			if test ! -z "$external_path_file" ; then
				AC_MSG_WARN([
--with-default-path=PATH will only be used if PATH is not defined in
$external_path_file .])
			fi
			user_path="$withval"
			SERVER_PATH_MSG="$withval"
		fi
	],
	[ if test "x$external_path_file" = "x/etc/login.conf" ; then
		AC_MSG_WARN([Make sure the path to scp is in /etc/login.conf])
	else
		if test ! -z "$external_path_file" ; then
			AC_MSG_WARN([
If PATH is defined in $external_path_file, ensure the path to scp is included,
otherwise scp will not work.])
		fi
		AC_RUN_IFELSE(
			[AC_LANG_PROGRAM([[
/* find out what STDPATH is */
#include <stdio.h>
#ifdef HAVE_PATHS_H
# include <paths.h>
#endif
#ifndef _PATH_STDPATH
# ifdef _PATH_USERPATH	/* Irix */
#  define _PATH_STDPATH _PATH_USERPATH
# else
#  define _PATH_STDPATH "/usr/bin:/bin:/usr/sbin:/sbin"
# endif
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#define DATA "conftest.stdpath"
			]], [[
	FILE *fd;
	int rc;

	fd = fopen(DATA,"w");
	if(fd == NULL)
		exit(1);

	if ((rc = fprintf(fd,"%s", _PATH_STDPATH)) < 0)
		exit(1);

	exit(0);
		]])],
		[ user_path=`cat conftest.stdpath` ],
		[ user_path="/usr/bin:/bin:/usr/sbin:/sbin" ],
		[ user_path="/usr/bin:/bin:/usr/sbin:/sbin" ]
	)
# make sure $bindir is in USER_PATH so scp will work
		t_bindir="${bindir}"
		while echo "${t_bindir}" | egrep '\$\{|NONE/' >/dev/null 2>&1; do
			t_bindir=`eval echo ${t_bindir}`
			case $t_bindir in
				NONE/*) t_bindir=`echo $t_bindir | sed "s~NONE~$prefix~"` ;;
			esac
			case $t_bindir in
				NONE/*) t_bindir=`echo $t_bindir | sed "s~NONE~$ac_default_prefix~"` ;;
			esac
		done
		echo $user_path | grep ":$t_bindir"  > /dev/null 2>&1
		if test $? -ne 0  ; then
			echo $user_path | grep "^$t_bindir"  > /dev/null 2>&1
			if test $? -ne 0  ; then
				user_path=$user_path:$t_bindir
				AC_MSG_RESULT([Adding $t_bindir to USER_PATH so scp will work])
			fi
		fi
	fi ]
)
if test "x$external_path_file" != "x/etc/login.conf" ; then
	AC_DEFINE_UNQUOTED([USER_PATH], ["$user_path"], [Specify default $PATH])
	AC_SUBST([user_path])
fi

# Set superuser path separately to user path
AC_ARG_WITH([superuser-path],
	[  --with-superuser-path=  Specify different path for super-user],
	[
		if test -n "$withval"  &&  test "x$withval" != "xno"  &&  \
		    test "x${withval}" != "xyes"; then
			AC_DEFINE_UNQUOTED([SUPERUSER_PATH], ["$withval"],
				[Define if you want a different $PATH
				for the superuser])
			superuser_path=$withval
		fi
	]
)


AC_MSG_CHECKING([if we need to convert IPv4 in IPv6-mapped addresses])
IPV4_IN6_HACK_MSG="no"
AC_ARG_WITH(4in6,
	[  --with-4in6             Check for and convert IPv4 in IPv6 mapped addresses],
	[
		if test "x$withval" != "xno" ; then
			AC_MSG_RESULT([yes])
			AC_DEFINE([IPV4_IN_IPV6], [1],
				[Detect IPv4 in IPv6 mapped addresses
				and treat as IPv4])
			IPV4_IN6_HACK_MSG="yes"
		else
			AC_MSG_RESULT([no])
		fi
	], [
		if test "x$inet6_default_4in6" = "xyes"; then
			AC_MSG_RESULT([yes (default)])
			AC_DEFINE([IPV4_IN_IPV6])
			IPV4_IN6_HACK_MSG="yes"
		else
			AC_MSG_RESULT([no (default)])
		fi
	]
)

# Whether to enable BSD auth support
BSD_AUTH_MSG=no
AC_ARG_WITH([bsd-auth],
	[  --with-bsd-auth         Enable BSD auth support],
	[
		if test "x$withval" != "xno" ; then
			AC_DEFINE([BSD_AUTH], [1],
				[Define if you have BSD auth support])
			BSD_AUTH_MSG=yes
		fi
	]
)

# Where to place sshd.pid
piddir=/var/run
# make sure the directory exists
if test ! -d $piddir ; then
	piddir=`eval echo ${sysconfdir}`
	case $piddir in
		NONE/*) piddir=`echo $piddir | sed "s~NONE~$ac_default_prefix~"` ;;
	esac
fi

AC_ARG_WITH([pid-dir],
	[  --with-pid-dir=PATH     Specify location of sshd.pid file],
	[
		if test -n "$withval"  &&  test "x$withval" != "xno"  &&  \
		    test "x${withval}" != "xyes"; then
			piddir=$withval
			if test ! -d $piddir ; then
			AC_MSG_WARN([** no $piddir directory on this system **])
			fi
		fi
	]
)

AC_DEFINE_UNQUOTED([_PATH_SSH_PIDDIR], ["$piddir"],
	[Specify location of ssh.pid])
AC_SUBST([piddir])

dnl allow user to disable some login recording features
AC_ARG_ENABLE([lastlog],
	[  --disable-lastlog       disable use of lastlog even if detected [no]],
	[
		if test "x$enableval" = "xno" ; then
			AC_DEFINE([DISABLE_LASTLOG])
		fi
	]
)
AC_ARG_ENABLE([utmp],
	[  --disable-utmp          disable use of utmp even if detected [no]],
	[
		if test "x$enableval" = "xno" ; then
			AC_DEFINE([DISABLE_UTMP])
		fi
	]
)
AC_ARG_ENABLE([utmpx],
	[  --disable-utmpx         disable use of utmpx even if detected [no]],
	[
		if test "x$enableval" = "xno" ; then
			AC_DEFINE([DISABLE_UTMPX], [1],
				[Define if you don't want to use utmpx])
		fi
	]
)
AC_ARG_ENABLE([wtmp],
	[  --disable-wtmp          disable use of wtmp even if detected [no]],
	[
		if test "x$enableval" = "xno" ; then
			AC_DEFINE([DISABLE_WTMP])
		fi
	]
)
AC_ARG_ENABLE([wtmpx],
	[  --disable-wtmpx         disable use of wtmpx even if detected [no]],
	[
		if test "x$enableval" = "xno" ; then
			AC_DEFINE([DISABLE_WTMPX], [1],
				[Define if you don't want to use wtmpx])
		fi
	]
)
AC_ARG_ENABLE([libutil],
	[  --disable-libutil       disable use of libutil (login() etc.) [no]],
	[
		if test "x$enableval" = "xno" ; then
			AC_DEFINE([DISABLE_LOGIN])
		fi
	]
)
AC_ARG_ENABLE([pututline],
	[  --disable-pututline     disable use of pututline() etc. ([uw]tmp) [no]],
	[
		if test "x$enableval" = "xno" ; then
			AC_DEFINE([DISABLE_PUTUTLINE], [1],
				[Define if you don't want to use pututline()
				etc. to write [uw]tmp])
		fi
	]
)
AC_ARG_ENABLE([pututxline],
	[  --disable-pututxline    disable use of pututxline() etc. ([uw]tmpx) [no]],
	[
		if test "x$enableval" = "xno" ; then
			AC_DEFINE([DISABLE_PUTUTXLINE], [1],
				[Define if you don't want to use pututxline()
				etc. to write [uw]tmpx])
		fi
	]
)
AC_ARG_WITH([lastlog],
  [  --with-lastlog=FILE|DIR specify lastlog location [common locations]],
	[
		if test "x$withval" = "xno" ; then
			AC_DEFINE([DISABLE_LASTLOG])
		elif test -n "$withval"  &&  test "x${withval}" != "xyes"; then
			conf_lastlog_location=$withval
		fi
	]
)

dnl lastlog, [uw]tmpx? detection
dnl  NOTE: set the paths in the platform section to avoid the
dnl   need for command-line parameters
dnl lastlog and [uw]tmp are subject to a file search if all else fails

dnl lastlog detection
dnl  NOTE: the code itself will detect if lastlog is a directory
AC_MSG_CHECKING([if your system defines LASTLOG_FILE])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <utmp.h>
#ifdef HAVE_LASTLOG_H
#  include <lastlog.h>
#endif
#ifdef HAVE_PATHS_H
#  include <paths.h>
#endif
#ifdef HAVE_LOGIN_H
# include <login.h>
#endif
	]], [[ char *lastlog = LASTLOG_FILE; ]])],
		[ AC_MSG_RESULT([yes]) ],
		[
		AC_MSG_RESULT([no])
		AC_MSG_CHECKING([if your system defines _PATH_LASTLOG])
		AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <utmp.h>
#ifdef HAVE_LASTLOG_H
#  include <lastlog.h>
#endif
#ifdef HAVE_PATHS_H
#  include <paths.h>
#endif
		]], [[ char *lastlog = _PATH_LASTLOG; ]])],
		[ AC_MSG_RESULT([yes]) ],
		[
			AC_MSG_RESULT([no])
			system_lastlog_path=no
		])
])

if test -z "$conf_lastlog_location"; then
	if test x"$system_lastlog_path" = x"no" ; then
		for f in /var/log/lastlog /usr/adm/lastlog /var/adm/lastlog /etc/security/lastlog ; do
				if (test -d "$f" || test -f "$f") ; then
					conf_lastlog_location=$f
				fi
		done
		if test -z "$conf_lastlog_location"; then
			AC_MSG_WARN([** Cannot find lastlog **])
			dnl Don't define DISABLE_LASTLOG - that means we don't try wtmp/wtmpx
		fi
	fi
fi

if test -n "$conf_lastlog_location"; then
	AC_DEFINE_UNQUOTED([CONF_LASTLOG_FILE], ["$conf_lastlog_location"],
		[Define if you want to specify the path to your lastlog file])
fi

dnl utmp detection
AC_MSG_CHECKING([if your system defines UTMP_FILE])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <utmp.h>
#ifdef HAVE_PATHS_H
#  include <paths.h>
#endif
	]], [[ char *utmp = UTMP_FILE; ]])],
	[ AC_MSG_RESULT([yes]) ],
	[ AC_MSG_RESULT([no])
	  system_utmp_path=no
])
if test -z "$conf_utmp_location"; then
	if test x"$system_utmp_path" = x"no" ; then
		for f in /etc/utmp /usr/adm/utmp /var/run/utmp; do
			if test -f $f ; then
				conf_utmp_location=$f
			fi
		done
		if test -z "$conf_utmp_location"; then
			AC_DEFINE([DISABLE_UTMP])
		fi
	fi
fi
if test -n "$conf_utmp_location"; then
	AC_DEFINE_UNQUOTED([CONF_UTMP_FILE], ["$conf_utmp_location"],
		[Define if you want to specify the path to your utmp file])
fi

dnl wtmp detection
AC_MSG_CHECKING([if your system defines WTMP_FILE])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <utmp.h>
#ifdef HAVE_PATHS_H
#  include <paths.h>
#endif
	]], [[ char *wtmp = WTMP_FILE; ]])],
	[ AC_MSG_RESULT([yes]) ],
	[ AC_MSG_RESULT([no])
	  system_wtmp_path=no
])
if test -z "$conf_wtmp_location"; then
	if test x"$system_wtmp_path" = x"no" ; then
		for f in /usr/adm/wtmp /var/log/wtmp; do
			if test -f $f ; then
				conf_wtmp_location=$f
			fi
		done
		if test -z "$conf_wtmp_location"; then
			AC_DEFINE([DISABLE_WTMP])
		fi
	fi
fi
if test -n "$conf_wtmp_location"; then
	AC_DEFINE_UNQUOTED([CONF_WTMP_FILE], ["$conf_wtmp_location"],
		[Define if you want to specify the path to your wtmp file])
fi

dnl wtmpx detection
AC_MSG_CHECKING([if your system defines WTMPX_FILE])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <utmp.h>
#ifdef HAVE_UTMPX_H
#include <utmpx.h>
#endif
#ifdef HAVE_PATHS_H
#  include <paths.h>
#endif
	]], [[ char *wtmpx = WTMPX_FILE; ]])],
	[ AC_MSG_RESULT([yes]) ],
	[ AC_MSG_RESULT([no])
	  system_wtmpx_path=no
])
if test -z "$conf_wtmpx_location"; then
	if test x"$system_wtmpx_path" = x"no" ; then
		AC_DEFINE([DISABLE_WTMPX])
	fi
else
	AC_DEFINE_UNQUOTED([CONF_WTMPX_FILE], ["$conf_wtmpx_location"],
		[Define if you want to specify the path to your wtmpx file])
fi


if test ! -z "$blibpath" ; then
	LDFLAGS="$LDFLAGS $blibflags$blibpath"
	AC_MSG_WARN([Please check and edit blibpath in LDFLAGS in Makefile])
fi

AC_CHECK_MEMBER([struct lastlog.ll_line], [], [
    if test x$SKIP_DISABLE_LASTLOG_DEFINE != "xyes" ; then
	AC_DEFINE([DISABLE_LASTLOG])
    fi
	], [
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_UTMP_H
#include <utmp.h>
#endif
#ifdef HAVE_UTMPX_H
#include <utmpx.h>
#endif
#ifdef HAVE_LASTLOG_H
#include <lastlog.h>
#endif
	])

AC_CHECK_MEMBER([struct utmp.ut_line], [], [
	AC_DEFINE([DISABLE_UTMP])
	AC_DEFINE([DISABLE_WTMP])
	], [
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_UTMP_H
#include <utmp.h>
#endif
#ifdef HAVE_UTMPX_H
#include <utmpx.h>
#endif
#ifdef HAVE_LASTLOG_H
#include <lastlog.h>
#endif
	])

dnl Adding -Werror to CFLAGS early prevents configure tests from running.
dnl Add now.
CFLAGS="$CFLAGS $werror_flags"

if test "x$ac_cv_func_getaddrinfo" != "xyes" ; then
	TEST_SSH_IPV6=no
else
	TEST_SSH_IPV6=yes
fi
AC_CHECK_DECL([BROKEN_GETADDRINFO],  [TEST_SSH_IPV6=no])
AC_SUBST([TEST_SSH_IPV6], [$TEST_SSH_IPV6])
AC_SUBST([TEST_SSH_UTF8], [$TEST_SSH_UTF8])
AC_SUBST([TEST_MALLOC_OPTIONS], [$TEST_MALLOC_OPTIONS])
AC_SUBST([UNSUPPORTED_ALGORITHMS], [$unsupported_algorithms])
AC_SUBST([DEPEND], [$(cat $srcdir/.depend)])

CFLAGS="${CFLAGS} ${CFLAGS_AFTER}"
LDFLAGS="${LDFLAGS} ${LDFLAGS_AFTER}"

AC_EXEEXT
AC_CONFIG_FILES([Makefile buildpkg.sh opensshd.init openssh.xml \
	openbsd-compat/Makefile openbsd-compat/regress/Makefile \
	survey.sh])
AC_OUTPUT

# Print summary of options

# Someone please show me a better way :)
A=`eval echo ${prefix}` ; A=`eval echo ${A}`
B=`eval echo ${bindir}` ; B=`eval echo ${B}`
C=`eval echo ${sbindir}` ; C=`eval echo ${C}`
D=`eval echo ${sysconfdir}` ; D=`eval echo ${D}`
E=`eval echo ${libexecdir}/ssh-askpass` ; E=`eval echo ${E}`
F=`eval echo ${mandir}/${mansubdir}X` ; F=`eval echo ${F}`
G=`eval echo ${piddir}` ; G=`eval echo ${G}`
H=`eval echo ${PRIVSEP_PATH}` ; H=`eval echo ${H}`
I=`eval echo ${user_path}` ; I=`eval echo ${I}`
J=`eval echo ${superuser_path}` ; J=`eval echo ${J}`

echo ""
echo "OpenSSH has been configured with the following options:"
echo "                     User binaries: $B"
echo "                   System binaries: $C"
echo "               Configuration files: $D"
echo "                   Askpass program: $E"
echo "                      Manual pages: $F"
echo "                          PID file: $G"
echo "  Privilege separation chroot path: $H"
if test "x$external_path_file" = "x/etc/login.conf" ; then
echo "   At runtime, sshd will use the path defined in $external_path_file"
echo "   Make sure the path to scp is present, otherwise scp will not work"
else
echo "            sshd default user PATH: $I"
	if test ! -z "$external_path_file"; then
echo "   (If PATH is set in $external_path_file it will be used instead. If"
echo "   used, ensure the path to scp is present, otherwise scp will not work.)"
	fi
fi
if test ! -z "$superuser_path" ; then
echo "          sshd superuser user PATH: $J"
fi
echo "                    Manpage format: $MANTYPE"
echo "                       PAM support: $PAM_MSG"
echo "                   OSF SIA support: $SIA_MSG"
echo "                 KerberosV support: $KRB5_MSG"
echo "                   SELinux support: $SELINUX_MSG"
echo "              TCP Wrappers support: $TCPW_MSG"
echo "              MD5 password support: $MD5_MSG"
echo "                   libedit support: $LIBEDIT_MSG"
echo "                   libldns support: $LDNS_MSG"
echo "  Solaris process contract support: $SPC_MSG"
echo "           Solaris project support: $SP_MSG"
echo "         Solaris privilege support: $SPP_MSG"
echo "       IP address in \$DISPLAY hack: $DISPLAY_HACK_MSG"
echo "           Translate v4 in v6 hack: $IPV4_IN6_HACK_MSG"
echo "                  BSD Auth support: $BSD_AUTH_MSG"
echo "              Random number source: $RAND_MSG"
echo "             Privsep sandbox style: $SANDBOX_STYLE"

echo ""

echo "              Host: ${host}"
echo "          Compiler: ${CC}"
echo "    Compiler flags: ${CFLAGS}"
echo "Preprocessor flags: ${CPPFLAGS}"
echo "      Linker flags: ${LDFLAGS}"
echo "         Libraries: ${LIBS}"
if test ! -z "${SSHDLIBS}"; then
echo "         +for sshd: ${SSHDLIBS}"
fi
if test ! -z "${SSHLIBS}"; then
echo "          +for ssh: ${SSHLIBS}"
fi

echo ""

if test "x$MAKE_PACKAGE_SUPPORTED" = "xyes" ; then
	echo "SVR4 style packages are supported with \"make package\""
	echo ""
fi

if test "x$PAM_MSG" = "xyes" ; then
	echo "PAM is enabled. You may need to install a PAM control file "
	echo "for sshd, otherwise password authentication may fail. "
	echo "Example PAM control files can be found in the contrib/ "
	echo "subdirectory"
	echo ""
fi

if test ! -z "$NO_PEERCHECK" ; then
	echo "WARNING: the operating system that you are using does not"
	echo "appear to support getpeereid(), getpeerucred() or the"
	echo "SO_PEERCRED getsockopt() option. These facilities are used to"
	echo "enforce security checks to prevent unauthorised connections to"
	echo "ssh-agent. Their absence increases the risk that a malicious"
	echo "user can connect to your agent."
	echo ""
fi

if test "$AUDIT_MODULE" = "bsm" ; then
	echo "WARNING: BSM audit support is currently considered EXPERIMENTAL."
	echo "See the Solaris section in README.platform for details."
fi
