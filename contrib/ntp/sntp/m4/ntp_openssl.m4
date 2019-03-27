dnl ####################################################################
dnl OpenSSL support shared by top-level and sntp/configure.ac
dnl
dnl Provides command-line option --with-crypto, as well as deprecated
dnl options --with-openssl-incdir, --with-openssl-libdir, and the
dnl latter's suboption --with-rpath.
dnl
dnl Specifying --with-openssl-libdir or --with-openssl-incdir causes any
dnl pkg-config openssl information to be ignored in favor of the legacy
dnl manual search for directories and specified library names.
dnl
dnl Output AC_DEFINEs (for config.h)
dnl	OPENSSL		defined only if using OpenSSL
dnl
dnl Output variables:
dnl	ntp_openssl	yes if using OpenSSL, no otherwise
dnl
dnl Output substitutions:
dnl	CFLAGS_NTP	OpenSSL-specific flags added as needed, and
dnl			-Wstrict-prototypes for gcc if it does not
dnl			trigger a flood of warnings for each file
dnl			including OpenSSL headers.
dnl	CPPFLAGS_NTP	OpenSSL -Iincludedir flags added as needed.
dnl	LDADD_NTP	OpenSSL -L and -l flags added as needed.
dnl	LDFLAGS_NTP	Other OpenSSL link flags added as needed.
dnl
dnl ####################################################################
AC_DEFUN([NTP_OPENSSL], [
AC_REQUIRE([NTP_PKG_CONFIG])dnl
AC_REQUIRE([NTP_VER_SUFFIX])dnl

AC_ARG_WITH(
    [crypto],
    [AS_HELP_STRING(
	[--with-crypto],
	[+ =openssl,libcrypto]
    )]
)
AC_ARG_WITH(
    [openssl-libdir],
    [AS_HELP_STRING(
	[--with-openssl-libdir], 
	[+ =/something/reasonable]
    )]
)
AC_ARG_WITH(
    [openssl-incdir],
    [AS_HELP_STRING(
	[--with-openssl-incdir],
	[+ =/something/reasonable]
    )]
)
AC_ARG_WITH(
    [rpath],
    [AS_HELP_STRING(
	[--without-rpath],
	[s Disable auto-added -R linker paths]
    )]
)

ntp_openssl=no
ntp_openssl_from_pkg_config=no

with_crypto=${with_crypto:-openssl,libcrypto}
case "$with_crypto" in
 yes)
    with_crypto=openssl,libcrypto
esac

dnl AC_MSG_NOTICE(['%with_crypto:%{PKG_CONFIG:+notempty}:%{with_openssl_libdir-notgiven}:%{with_openssl_incdir-notgiven}'])
dnl str="$with_crypto:${PKG_CONFIG:+notempty}:${with_openssl_libdir-notgiven}:${with_openssl_incdir-notgiven}"
dnl AC_MSG_NOTICE([$str])

case "$with_crypto:${PKG_CONFIG:+notempty}:${with_openssl_libdir-notgiven}:${with_openssl_incdir-notgiven}" in
 no:*) ;;
 *:notempty:notgiven:notgiven)
    for pkg in `echo $with_crypto | sed -e 's/,/ /'`; do
	AC_MSG_CHECKING([pkg-config for $pkg])
	if $PKG_CONFIG --exists $pkg ; then
	    CPPFLAGS_NTP="$CPPFLAGS_NTP `$PKG_CONFIG --cflags-only-I $pkg`"
	    CFLAGS_NTP="$CFLAGS_NTP `$PKG_CONFIG --cflags-only-other $pkg`"
	    LDADD_NTP="$LDADD_NTP `$PKG_CONFIG --libs-only-L $pkg`"
	    LDADD_NTP="$LDADD_NTP `$PKG_CONFIG --libs-only-l --static $pkg`"
	    LDFLAGS_NTP="$LDFLAGS_NTP `$PKG_CONFIG --libs-only-other $pkg`"
	    VER_SUFFIX=o
	    ntp_openssl=yes
	    ntp_openssl_from_pkg_config=yes
	    ntp_openssl_version="`$PKG_CONFIG --modversion $pkg`"
	    case "$ntp_openssl_version" in
	     *.*) ;;
	     *) ntp_openssl_version='(unknown)' ;;
	    esac
	    AC_MSG_RESULT([yes, version $ntp_openssl_version])

	    break
	fi
	AC_MSG_RESULT([no])
    done
esac
dnl AC_MSG_NOTICE([OpenSSL Phase I checks:])
dnl AC_MSG_NOTICE([CPPFLAGS_NTP: $CPPFLAGS_NTP])
dnl AC_MSG_NOTICE([CFLAGS_NTP: $CFLAGS_NTP])
dnl AC_MSG_NOTICE([LDADD_NTP: $LDADD_NTP])
dnl AC_MSG_NOTICE([LDFLAGS_NTP: $LDFLAGS_NTP])
case "$with_crypto:$ntp_openssl" in
 no:*) ;;
 *:no)
    need_dash_r=
    need_dash_Wlrpath=
    case "${with_rpath-notgiven}" in
     yes)
	# Lame - what to do if we need -Wl... but not -R?
	need_dash_r=1
	;;
     notgiven)
	case "$host" in
	 *-*-linux*)
	    # This may really only be true for gcc
	    need_dash_Wlrpath=1
	    ;;
	 *-*-netbsd*)
	    need_dash_r=1
	    ;;
	 *-*-solaris*)
	    need_dash_r=1
	    ;;
	esac
	;;
    esac

    AC_MSG_CHECKING([for openssl library directory])
    with_openssl_libdir=${with_openssl_libdir-notgiven}
    case "$with_openssl_libdir" in
     notgiven)
	case "$build" in
	 $host)
	    with_openssl_libdir=default
	    ;;
	 *)
	    with_openssl_libdir=no
	    ;;
	esac
    esac
    case "$with_openssl_libdir" in
     default)
	# Look in:
	with_openssl_libdir="/usr/lib /usr/lib/openssl /usr/sfw/lib"
	with_openssl_libdir="$with_openssl_libdir /usr/local/lib"
	with_openssl_libdir="$with_openssl_libdir /usr/local/ssl/lib /lib"
    esac
    case "$with_openssl_libdir" in
     no)
	;;
     *) # Look for libcrypto.a and libssl.a:
	for i in $with_openssl_libdir no
	do
	    case "$host" in
	     *-*-darwin*)
		test -f $i/libcrypto.dylib -a -f $i/libssl.dylib && break
		;;
	     *)
		test -f $i/libcrypto.so -a -f $i/libssl.so && break
		test -f $i/libcrypto.a -a -f $i/libssl.a && break
		;;
	    esac
	done
	openssl_libdir=$i
	;;
    esac
    AC_MSG_RESULT([$openssl_libdir])
    case "$openssl_libdir" in
     no)
	openssl_libdir=
	AC_MSG_WARN([libcrypto and libssl not found in any of $with_openssl_libdir])
    esac

    AC_MSG_CHECKING([for openssl include directory])
    with_openssl_incdir=${with_openssl_incdir-notgiven}
    case "$with_openssl_incdir" in
     notgiven)
	# Look in:
	with_openssl_incdir="/usr/include /usr/sfw/include"
	with_openssl_incdir="$with_openssl_incdir /usr/local/include"
	with_openssl_incdir="$with_openssl_incdir /usr/local/ssl/include"
    esac
    case "$with_openssl_incdir" in
     no)
	;;
     *) # look for openssl/evp.h:
	for i in $with_openssl_incdir no
	do
	    test -f $i/openssl/evp.h && break
	done
	openssl_incdir=$i
	;;
    esac
    AS_UNSET([i])
    AC_MSG_RESULT([$openssl_incdir])
    case "$openssl_incdir" in
     no)
	openssl_incdir=
	AC_MSG_WARN([did not find openssl/evp.h in any of $with_openssl_incdir])
    esac
    if test -z "$openssl_libdir" -o -z "$openssl_incdir"
    then
	ntp_openssl=no
    else
	ntp_openssl=yes
	VER_SUFFIX=o
    fi
    case "$ntp_openssl" in
     yes)
	# We have OpenSSL inc/lib dirs - use them.
	case "$openssl_incdir" in
	 /usr/include)
	    ;;
	 *)
	    CPPFLAGS_NTP="$CPPFLAGS_NTP -I$openssl_incdir"
	    ;;
	esac
	case "$openssl_libdir" in
	 /usr/lib)
	    ;;
	 *)
	    LDADD_NTP="$LDADD_NTP -L$openssl_libdir"
	    case "$need_dash_r" in
	     1)
		LDFLAGS_NTP="$LDFLAGS_NTP -R$openssl_libdir"
	    esac
	    case "$need_dash_Wlrpath" in
	     1)
		LDFLAGS_NTP="$LDFLAGS_NTP -Wl,-rpath,$openssl_libdir"
	    esac
	    ;;
	esac
	LDADD_NTP="$LDADD_NTP -lcrypto"
    esac
esac

AC_MSG_CHECKING([if we will use crypto])
AC_MSG_RESULT([$ntp_openssl])

case "$ntp_openssl" in
 yes)
    AC_CHECK_HEADERS([openssl/cmac.h openssl/hmac.h])
    AC_DEFINE([OPENSSL], [], [Use OpenSSL?])
    case "$VER_SUFFIX" in
     *o*) ;;
     *) AC_MSG_ERROR([OPENSSL set but no 'o' in VER_SUFFIX!]) ;;
    esac
    ;;
esac

NTPO_SAVED_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $CPPFLAGS_NTP"
NTPO_SAVED_LIBS="$LIBS"

#
# check for linking with -lcrypto failure, and try -lcrypto -lz.
# Helps m68k-atari-mint
#
case "$ntp_openssl:$ntp_openssl_from_pkg_config" in
 yes:no)
    LIBS="$NTPO_SAVED_LIBS $LDADD_NTP"
    AC_CACHE_CHECK(
	[if linking with -lcrypto alone works],
	[ntp_cv_bare_lcrypto],
	[AC_LINK_IFELSE(
	    [AC_LANG_PROGRAM(
		[[
		    #include "openssl/err.h"
		    #include "openssl/evp.h"
		]],
		[[
		    ERR_load_crypto_strings();
		    OpenSSL_add_all_algorithms();
		]]
	    )],
	    [ntp_cv_bare_lcrypto=yes],
	    [ntp_cv_bare_lcrypto=no]
	)]
    )
    case "$ntp_cv_bare_lcrypto" in
     no)
	LIBS="$NTPO_SAVED_LIBS $LDADD_NTP -lz"
	AC_CACHE_CHECK(
	    [if linking with -lcrypto -lz works],
	    [ntp_cv_lcrypto_lz],
	    [AC_LINK_IFELSE(
		[AC_LANG_PROGRAM(
		    [[
			#include "openssl/err.h"
			#include "openssl/evp.h"
		    ]],
		    [[
			ERR_load_crypto_strings();
			OpenSSL_add_all_algorithms();
		    ]]
		)],
		[ntp_cv_lcrypto_lz=yes],
		[ntp_cv_lcrypto_lz=no]
	    )]
	)
	case "$ntp_cv_lcrypto_lz" in
	 yes)
	     LDADD_NTP="$LDADD_NTP -lz"
	esac
    esac
esac

#
# Older OpenSSL headers have a number of callback prototypes inside
# other function prototypes which trigger copious warnings with gcc's
# -Wstrict-prototypes, which is included in -Wall.
#
# An example:
#
# int i2d_RSA_NET(const RSA *a, unsigned char **pp, 
#		  int (*cb)(), int sgckey);
#		  ^^^^^^^^^^^
#
# 
#
openssl_triggers_warnings=unknown
NTPO_SAVED_CFLAGS="$CFLAGS"

case "$ntp_openssl:$GCC" in
 yes:yes)
    CFLAGS="$CFLAGS -Werror"
    AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM(
	    [[
	    ]],
	    [[
		/* see if -Werror breaks gcc */
	    ]]
	)],
	[gcc_handles_Werror=yes],
	[gcc_handles_Werror=no]
    )
    case "$gcc_handles_Werror" in
     no)
	# if this gcc doesn't do -Werror go ahead and use
	# -Wstrict-prototypes.
	openssl_triggers_warnings=yes
	;;
     yes)
	CFLAGS="$CFLAGS -Wstrict-prototypes"
	AC_COMPILE_IFELSE(
	    [AC_LANG_PROGRAM(
		[[
		    #include "openssl/asn1_mac.h"
		    #include "openssl/bn.h"
		    #include "openssl/err.h"
		    #include "openssl/evp.h"
		    #include "openssl/pem.h"
		    #include "openssl/rand.h"
		    #include "openssl/x509v3.h"
		]],
		[[
		    /* empty body */
		]]
	    )],
	    [openssl_triggers_warnings=no],
	    [openssl_triggers_warnings=yes]
	)
    esac
    case "$openssl_triggers_warnings" in
     yes)
	CFLAGS_NTP="$CFLAGS_NTP -Wno-strict-prototypes"
	;;
     *)
	CFLAGS_NTP="$CFLAGS_NTP -Wstrict-prototypes"
    esac
    ;;
 no:yes)
    # gcc without OpenSSL
    CFLAGS_NTP="$CFLAGS_NTP -Wstrict-prototypes"
esac

# Because we don't want -Werror for the EVP_MD_do_all_sorted check
CFLAGS="$NTPO_SAVED_CFLAGS"

case "$ntp_openssl" in
 yes)
    LIBS="$NTPO_SAVED_LIBS $LDADD_NTP"
    AC_CHECK_FUNCS([EVP_MD_do_all_sorted])
    ;;
esac

dnl AC_MSG_NOTICE([OpenSSL final checks:])
dnl AC_MSG_NOTICE([CPPFLAGS_NTP: $CPPFLAGS_NTP])
dnl AC_MSG_NOTICE([CFLAGS_NTP: $CFLAGS_NTP])
dnl AC_MSG_NOTICE([LDADD_NTP: $LDADD_NTP])
dnl AC_MSG_NOTICE([LDFLAGS_NTP: $LDFLAGS_NTP])

CPPFLAGS="$NTPO_SAVED_CPPFLAGS"
LIBS="$NTPO_SAVED_LIBS"
AS_UNSET([NTPO_SAVED_CFLAGS])
AS_UNSET([NTPO_SAVED_CPPFLAGS])
AS_UNSET([NTPO_SAVED_LIBS])
AS_UNSET([openssl_triggers_warnings])
AS_UNSET([ntp_openssl_from_pkg_config])
])
dnl ======================================================================
