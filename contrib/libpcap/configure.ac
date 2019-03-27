dnl
dnl Copyright (c) 1994, 1995, 1996, 1997
dnl	The Regents of the University of California.  All rights reserved.
dnl
dnl Process this file with autoconf to produce a configure script.
dnl

#
# See
#
#	http://ftp.gnu.org/gnu/config/README
#
# for the URLs to use to fetch new versions of config.guess and
# config.sub.
#

AC_PREREQ(2.64)

AC_INIT(pcap, m4_esyscmd_s([cat VERSION]))
AC_CONFIG_SRCDIR(pcap.c)
AC_SUBST(PACKAGE_NAME)

AC_CANONICAL_SYSTEM

AC_LBL_C_INIT_BEFORE_CC(V_CCOPT, V_INCLS)
#
# Try to enable as many C99 features as we can.
# At minimum, we want C++/C99-style // comments.
#
AC_PROG_CC_C99
AC_LBL_C_INIT(V_CCOPT, V_INCLS)
AC_LBL_SHLIBS_INIT
AC_LBL_C_INLINE

#
# Try to arrange for large file support.
#
AC_SYS_LARGEFILE
AC_FUNC_FSEEKO

dnl
dnl Even if <net/bpf.h> were, on all OSes that support BPF, fixed to
dnl include <sys/ioccom.h>, and we were to drop support for older
dnl releases without that fix, so that pcap-bpf.c doesn't need to
dnl include <sys/ioccom.h>, the test program in "AC_LBL_FIXINCLUDES"
dnl in "aclocal.m4" uses it, so we would still have to test for it
dnl and set "HAVE_SYS_IOCCOM_H" if we have it, otherwise
dnl "AC_LBL_FIXINCLUDES" wouldn't work on some platforms such as Solaris.
dnl
AC_CHECK_HEADERS(sys/ioccom.h sys/sockio.h limits.h)
AC_CHECK_HEADERS(netpacket/packet.h)
AC_CHECK_HEADERS(net/pfvar.h, , , [#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>])
if test "$ac_cv_header_net_pfvar_h" = yes; then
	#
	# Check for various PF actions.
	#
	AC_MSG_CHECKING(whether net/pfvar.h defines PF_NAT through PF_NORDR)
	AC_TRY_COMPILE(
	    [#include <sys/types.h>
	    #include <sys/socket.h>
	    #include <net/if.h>
	    #include <net/pfvar.h>],
	    [return PF_NAT+PF_NONAT+PF_BINAT+PF_NOBINAT+PF_RDR+PF_NORDR;],
	    [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PF_NAT_THROUGH_PF_NORDR, 1,
		    [define if net/pfvar.h defines PF_NAT through PF_NORDR])
	    ],
	    AC_MSG_RESULT(no))
fi

case "$host_os" in
linux*|uclinux*)
	AC_CHECK_HEADERS(linux/sockios.h linux/if_bonding.h,,,
	[
#include <sys/socket.h>
#include <linux/if.h>
	])
	;;
esac

AC_LBL_FIXINCLUDES

AC_CHECK_FUNCS(strerror strerror_r strerror_s strlcpy strlcat)

needsnprintf=no
AC_CHECK_FUNCS(vsnprintf snprintf,,
	[needsnprintf=yes])
if test $needsnprintf = yes; then
	AC_LIBOBJ([snprintf])
fi

needstrtok_r=no
AC_CHECK_FUNCS(strtok_r,,
	[needstrtok_r=yes])
if test $needstrtok_r = yes; then
	AC_LIBOBJ([strtok_r])
fi

#
# Do we have ffs(), and is it declared in <strings.h>?
#
AC_CHECK_FUNCS(ffs)
if test "$ac_cv_func_ffs" = yes; then
	#
	# We have ffs(); is it declared in <strings.h>?
	#
	# This test fails if we don't have <strings.h> or if we do
	# but it doesn't declare ffs().
	#
	AC_CHECK_DECL(ffs,
	    [
		AC_DEFINE(STRINGS_H_DECLARES_FFS,,
		    [Define to 1 if strings.h declares `ffs'])
	    ],,
	    [
#include <strings.h>
	    ])
fi

#
# Do this before checking for ether_hostton(), as it's a
# "getaddrinfo()-ish function".
#
AC_LBL_LIBRARY_NET

#
# Check for reentrant versions of getnetbyname_r(), as provided by
# Linux (glibc), Solaris/IRIX, and AIX (with three different APIs!).
# If we don't find one, we just use getnetbyname(), which uses
# thread-specific data on many platforms, but doesn't use it on
# NetBSD or OpenBSD, and may not use it on older versions of other
# platforms.
#
# Only do the check if we have a declaration of getnetbyname_r();
# without it, we can't check which API it has.  (We assume that
# if there's a declaration, it has a prototype, so that the API
# can be checked.)
#
AC_CHECK_DECL(getnetbyname_r,
    [
	AC_MSG_CHECKING([for the Linux getnetbyname_r()])
	AC_TRY_LINK(
	    [#include <netdb.h>],
	    [
		struct netent netent_buf;
		char buf[1024];
		struct netent *resultp;
		int h_errnoval;

		return getnetbyname_r((const char *)0, &netent_buf, buf, sizeof buf, &resultp, &h_errnoval);
	    ],
	    [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_LINUX_GETNETBYNAME_R, 1,
		    [define if we have the Linux getnetbyname_r()])
	    ],
	    [
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([for Solaris/IRIX getnetbyname_r()])
		AC_TRY_LINK(
		    [#include <netdb.h>],
		    [
			struct netent netent_buf;
			char buf[1024];

			return getnetbyname_r((const char *)0, &netent_buf, buf, (int)sizeof buf) != NULL;
		    ],
		    [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_SOLARIS_IRIX_GETNETBYNAME_R, 1,
			    [define if we have the Solaris/IRIX getnetbyname_r()])
		    ],
		    [
			AC_MSG_RESULT(no)

			AC_MSG_CHECKING([for AIX getnetbyname_r()])
			AC_TRY_LINK(
			    [#include <netdb.h>],
			    [
				struct netent netent_buf;
				struct netent_data net_data;

				return getnetbyname_r((const char *)0, &netent_buf, &net_data);
			    ],
			    [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_AIX_GETNETBYNAME_R, 1,
				    [define if we have the AIX getnetbyname_r()])
			    ],
			    [
				AC_MSG_RESULT(no)
			    ])
		    ])
	    ])
    ],,[#include <netdb.h>])

#
# Check for reentrant versions of getprotobyname_r(), as provided by
# Linux (glibc), Solaris/IRIX, and AIX (with three different APIs!).
# If we don't find one, we just use getprotobyname(), which uses
# thread-specific data on many platforms, but doesn't use it on
# NetBSD or OpenBSD, and may not use it on older versions of other
# platforms.
#
# Only do the check if we have a declaration of getprotobyname_r();
# without it, we can't check which API it has.  (We assume that
# if there's a declaration, it has a prototype, so that the API
# can be checked.)
#
AC_CHECK_DECL(getprotobyname_r,
    [
	AC_MSG_CHECKING([for the Linux getprotobyname_r()])
	AC_TRY_LINK(
	    [#include <netdb.h>],
	    [
		struct protoent protoent_buf;
		char buf[1024];
		struct protoent *resultp;

		return getprotobyname_r((const char *)0, &protoent_buf, buf, sizeof buf, &resultp);
	    ],
	    [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_LINUX_GETPROTOBYNAME_R, 1,
		    [define if we have the Linux getprotobyname_r()])
	    ],
	    [
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([for Solaris/IRIX getprotobyname_r()])
		AC_TRY_LINK(
		    [#include <netdb.h>],
		    [
			struct protoent protoent_buf;
			char buf[1024];

			return getprotobyname_r((const char *)0, &protoent_buf, buf, (int)sizeof buf) != NULL;
		    ],
		    [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_SOLARIS_IRIX_GETPROTOBYNAME_R, 1,
			    [define if we have the Solaris/IRIX getprotobyname_r()])
		    ],
		    [
			AC_MSG_RESULT(no)

			AC_MSG_CHECKING([for AIX getprotobyname_r()])
			AC_TRY_LINK(
			    [#include <netdb.h>],
			    [
				struct protoent protoent_buf;
				struct protoent_data proto_data;

				return getprotobyname_r((const char *)0, &protoent_buf, &proto_data);
			    ],
			    [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_AIX_GETPROTOBYNAME_R, 1,
				    [define if we have the AIX getprotobyname_r()])
			    ],
			    [
				AC_MSG_RESULT(no)
			    ])
		    ])
	    ])
    ],,[#include <netdb.h>])

#
# You are in a twisty little maze of UN*Xes, all different.
# Some might not have ether_hostton().
# Some might have it and declare it in <net/ethernet.h>.
# Some might have it and declare it in <netinet/ether.h>
# Some might have it and declare it in <sys/ethernet.h>.
# Some might have it and declare it in <arpa/inet.h>.
# Some might have it and declare it in <netinet/if_ether.h>.
# Some might have it and not declare it in any header file.
#
# Before you is a C compiler.
#
AC_CHECK_FUNCS(ether_hostton)
if test "$ac_cv_func_ether_hostton" = yes; then
	#
	# OK, we have ether_hostton().  Is it declared in <net/ethernet.h>?
	#
	# This test fails if we don't have <net/ethernet.h> or if we do
	# but it doesn't declare ether_hostton().
	#
	AC_CHECK_DECL(ether_hostton,
	    [
		AC_DEFINE(NET_ETHERNET_H_DECLARES_ETHER_HOSTTON,,
		    [Define to 1 if net/ethernet.h declares `ether_hostton'])
	    ],,
	    [
#include <net/ethernet.h>
	    ])
	#
	# Did that succeed?
	#
	if test "$ac_cv_have_decl_ether_hostton" != yes; then
		#
		# No, how about <netinet/ether.h>, as on Linux?
		#
		# This test fails if we don't have <netinet/ether.h>
		# or if we do but it doesn't declare ether_hostton().
		#
		# Unset ac_cv_have_decl_ether_hostton so we don't
		# treat the previous failure as a cached value and
		# suppress the next test.
		#
		unset ac_cv_have_decl_ether_hostton
		AC_CHECK_DECL(ether_hostton,
		    [
			AC_DEFINE(NETINET_ETHER_H_DECLARES_ETHER_HOSTTON,,
			    [Define to 1 if netinet/ether.h declares `ether_hostton'])
		    ],,
		    [
#include <netinet/ether.h>
		    ])
	fi
	#
	# Did that succeed?
	#
	if test "$ac_cv_have_decl_ether_hostton" != yes; then
		#
		# No, how about <sys/ethernet.h>, as on Solaris 10
		# and later?
		#
		# This test fails if we don't have <sys/ethernet.h>
		# or if we do but it doesn't declare ether_hostton().
		#
		# Unset ac_cv_have_decl_ether_hostton so we don't
		# treat the previous failure as a cached value and
		# suppress the next test.
		#
		unset ac_cv_have_decl_ether_hostton
		AC_CHECK_DECL(ether_hostton,
		    [
			AC_DEFINE(SYS_ETHERNET_H_DECLARES_ETHER_HOSTTON,,
			    [Define to 1 if sys/ethernet.h declares `ether_hostton'])
		    ],,
		    [
#include <sys/ethernet.h>
		    ])
	fi
	#
	# Did that succeed?
	#
	if test "$ac_cv_have_decl_ether_hostton" != yes; then
		#
		# No, how about <arpa/inet.h>, as in AIX?
		#
		# This test fails if we don't have <arpa/inet.h>
		# (if we have ether_hostton(), we should have
		# networking, and if we have networking, we should
		# have <arapa/inet.h>) or if we do but it doesn't
		# declare ether_hostton().
		#
		# Unset ac_cv_have_decl_ether_hostton so we don't
		# treat the previous failure as a cached value and
		# suppress the next test.
		#
		unset ac_cv_have_decl_ether_hostton
		AC_CHECK_DECL(ether_hostton,
		    [
			AC_DEFINE(ARPA_INET_H_DECLARES_ETHER_HOSTTON,,
			    [Define to 1 if arpa/inet.h declares `ether_hostton'])
		    ],,
		    [
#include <arpa/inet.h>
		    ])
	fi
	#
	# Did that succeed?
	#
	if test "$ac_cv_have_decl_ether_hostton" != yes; then
		#
		# No, how about <netinet/if_ether.h>?
		# On some platforms, it requires <net/if.h> and
		# <netinet/in.h>, and we always include it with
		# both of them, so test it with both of them.
		#
		# This test fails if we don't have <netinet/if_ether.h>
		# and the headers we include before it, or if we do but
		# <netinet/if_ether.h> doesn't declare ether_hostton().
		#
		# Unset ac_cv_have_decl_ether_hostton so we don't
		# treat the previous failure as a cached value and
		# suppress the next test.
		#
		unset ac_cv_have_decl_ether_hostton
		AC_CHECK_DECL(ether_hostton,
		    [
			AC_DEFINE(NETINET_IF_ETHER_H_DECLARES_ETHER_HOSTTON,,
			    [Define to 1 if netinet/if_ether.h declares `ether_hostton'])
		    ],,
		    [
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
		    ])
	fi
	#
	# After all that, is ether_hostton() declared?
	#
	if test "$ac_cv_have_decl_ether_hostton" = yes; then
		#
		# Yes.
		#
		AC_DEFINE(HAVE_DECL_ETHER_HOSTTON, 1,
		    [Define to 1 if you have the declaration of `ether_hostton'])
        else
		#
		# No, we'll have to declare it ourselves.
		# Do we have "struct ether_addr" if we include
		# <netinet/if_ether.h>?
		#
		AC_CHECK_TYPES(struct ether_addr,,,
		    [
			#include <sys/types.h>
			#include <sys/socket.h>
			#include <net/if.h>
			#include <netinet/in.h>
			#include <netinet/if_ether.h>
		    ])
	fi
fi

#
# For various things that might use pthreads.
#
AC_CHECK_HEADER(pthread.h,
    [
	#
	# OK, we have pthread.h.  Do we have pthread_create in the
	# system libraries?
	#
	AC_CHECK_FUNC(pthread_create,
	    [
		#
		# Yes.
		#
		ac_lbl_have_pthreads="found"
	    ],
	    [
		#
		# No - do we have it in -lpthreads?
		#
		AC_CHECK_LIB(pthreads, pthread_create,
		    [
			#
			# Yes - add -lpthreads.
			#
			ac_lbl_have_pthreads="found"
			PTHREAD_LIBS="$PTHREAD_LIBS -lpthreads"
		    ],
		    [
			#
			# No - do we have it in -lpthread?
			#
			AC_CHECK_LIB(pthread, pthread_create,
			    [
				#
				# Yes - add -lpthread.
				#
                                ac_lbl_have_pthreads="found"
				PTHREAD_LIBS="$PTHREAD_LIBS -lpthread"
			    ],
			    [
				#
				# No.
				#
				ac_lbl_have_pthreads="not found"
			    ])
		    ])
	    ])
    ],
    [
	#
	# We didn't find pthread.h.
	#
	ac_lbl_have_pthreads="not found"
    ]
)

dnl to pacify those who hate protochain insn
AC_MSG_CHECKING(if --disable-protochain option is specified)
AC_ARG_ENABLE(protochain,
AC_HELP_STRING([--disable-protochain],[disable \"protochain\" insn]))
case "x$enable_protochain" in
xyes)	enable_protochain=enabled	;;
xno)	enable_protochain=disabled	;;
x)	enable_protochain=enabled	;;
esac

if test "$enable_protochain" = "disabled"; then
	AC_DEFINE(NO_PROTOCHAIN,1,[do not use protochain])
fi
AC_MSG_RESULT(${enable_protochain})

#
# valgrindtest directly uses the native capture mechanism, but
# only tests with BPF and PF_PACKET sockets; only enable it if
# we have BPF or PF_PACKET sockets.
#
VALGRINDTEST_SRC=

#
# SITA support is mutually exclusive with native capture support;
# "--with-sita" selects SITA support.
#
AC_ARG_WITH(sita,
AC_HELP_STRING([--with-sita],[include SITA support]),
[
	if test ! "x$withval" = "xno" ; then
		AC_DEFINE(SITA,1,[include ACN support])
		AC_MSG_NOTICE(Enabling SITA ACN support)
		V_PCAP=sita
	fi
],
[
AC_ARG_WITH(pcap,
AC_HELP_STRING([--with-pcap=TYPE],[use packet capture TYPE]))
if test ! -z "$with_pcap" ; then
	V_PCAP="$withval"
else
	#
	# Check for a bunch of headers for various packet
	# capture mechanisms.
	#
	AC_CHECK_HEADERS(net/bpf.h)
	if test "$ac_cv_header_net_bpf_h" = yes; then
		#
		# Does it define BIOCSETIF?
		# I.e., is it a header for an LBL/BSD-style capture
		# mechanism, or is it just a header for a BPF filter
		# engine?  Some versions of Arch Linux, for example,
		# have a net/bpf.h that doesn't define BIOCSETIF;
		# as it's a Linux, it should use packet sockets,
		# instead.
		#
		# We need:
		#
		#  sys/types.h, because FreeBSD 10's net/bpf.h
		#  requires that various BSD-style integer types
		#  be defined;
		#
		#  sys/ioctl.h and, if we have it, sys/ioccom.h,
		#  because net/bpf.h defines ioctls;
		#
		#  net/if.h, because it defines some structures
		#  used in ioctls defined by net/bpf.h;
		#
		#  sys/socket.h, because OpenBSD 5.9's net/bpf.h
		#  defines some structure fields as being
		#  struct sockaddrs;
		#
		# and net/bpf.h doesn't necessarily include all
		# of those headers itself.
		#
		AC_MSG_CHECKING(if net/bpf.h defines BIOCSETIF)
		AC_CACHE_VAL(ac_cv_lbl_bpf_h_defines_biocsetif,
			AC_TRY_COMPILE(
[
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_IOCCOM_H
#include <sys/ioccom.h>
#endif
#include <net/bpf.h>
#include <net/if.h>
],
			[u_int i = BIOCSETIF;],
			ac_cv_lbl_bpf_h_defines_biocsetif=yes,
			ac_cv_lbl_bpf_h_defines_biocsetif=no))
		AC_MSG_RESULT($ac_cv_lbl_bpf_h_defines_biocsetif)
	fi
	AC_CHECK_HEADERS(net/pfilt.h net/enet.h)
	AC_CHECK_HEADERS(net/nit.h sys/net/nit.h)
	AC_CHECK_HEADERS(linux/socket.h net/raw.h sys/dlpi.h)

	if test "$ac_cv_lbl_bpf_h_defines_biocsetif" = yes; then
		#
		# BPF.
		# Check this before DLPI, so that we pick BPF on
		# Solaris 11 and later.
		#
		V_PCAP=bpf

		#
		# We have BPF, so build valgrindtest with "make test"
		# on macOS and FreeBSD (add your OS once there's a
		# valgrind for it).
		#
		case "$host_os" in

		freebsd*|darwin*|linux*)
			VALGRINDTEST_SRC=valgrindtest.c
			;;
		esac
	elif test "$ac_cv_header_linux_socket_h" = yes; then
		#
		# No prizes for guessing this one.
		#
		V_PCAP=linux

		#
		# XXX - this won't work with older kernels that have
		# SOCK_PACKET sockets but not PF_PACKET sockets.
		#
		VALGRINDTEST_SRC=valgrindtest.c
	elif test "$ac_cv_header_net_pfilt_h" = yes; then
	        #
	        # DEC OSF/1, Digital UNIX, Tru64 UNIX
        	#
		V_PCAP=pf
	elif test "$ac_cv_header_net_enet_h" = yes; then
		#
		# Stanford Enetfilter.
		#
		V_PCAP=enet
	elif test "$ac_cv_header_net_nit_h" = yes; then
		#
		# SunOS 4.x STREAMS NIT.
		#
		V_PCAP=snit
	elif test "$ac_cv_header_sys_net_nit_h" = yes; then
		#
		# Pre-SunOS 4.x non-STREAMS NIT.
		#
		V_PCAP=nit
	elif test "$ac_cv_header_net_raw_h" = yes; then
		#
		# IRIX snoop.
		#
		V_PCAP=snoop
	elif test "$ac_cv_header_sys_dlpi_h" = yes; then
		#
		# DLPI on pre-Solaris 11 SunOS 5, HP-UX, possibly others.
		#
		V_PCAP=dlpi
	else
		#
		# Nothing we support.
		#
		V_PCAP=null
		AC_MSG_WARN(cannot determine packet capture interface)
		AC_MSG_WARN((see the INSTALL doc for more info))
	fi
fi
AC_MSG_CHECKING(packet capture type)
AC_MSG_RESULT($V_PCAP)
AC_SUBST(VALGRINDTEST_SRC)

#
# Do capture-mechanism-dependent tests.
#
case "$V_PCAP" in
dlpi)
	#
	# Needed for common functions used by pcap-[dlpi,libdlpi].c
	#
 	SSRC="dlpisubs.c"

	#
	# Checks for some header files.
	#
	AC_CHECK_HEADERS(sys/bufmod.h sys/dlpi_ext.h)

	#
	# Checks to see if Solaris has the public libdlpi(3LIB) library.
	# Note: The existence of /usr/include/libdlpi.h does not mean it is the
	# public libdlpi(3LIB) version. Before libdlpi was made public, a
	# private version also existed, which did not have the same APIs.
	# Due to a gcc bug, the default search path for 32-bit libraries does
	# not include /lib, we add it explicitly here.
	# [http://bugs.opensolaris.org/view_bug.do?bug_id=6619485].
	# Also, due to the bug above applications that link to libpcap with
	# libdlpi will have to add "-L/lib" option to "configure".
	#
	saved_ldflags=$LDFLAGS
	LDFLAGS="$LIBS -L/lib"
	AC_CHECK_LIB(dlpi, dlpi_walk,
		[
			LIBS="-ldlpi $LIBS"
			V_PCAP=libdlpi
			AC_DEFINE(HAVE_LIBDLPI,1,[if libdlpi exists])
		],
		V_PCAP=dlpi)
	LDFLAGS=$saved_ldflags

	#
	# Checks whether <sys/dlpi.h> is usable, to catch weird SCO
	# versions of DLPI.
	#
	AC_MSG_CHECKING(whether <sys/dlpi.h> is usable)
	AC_CACHE_VAL(ac_cv_sys_dlpi_usable,
		AC_TRY_COMPILE(
		    [
			#include <sys/types.h>
			#include <sys/time.h>
			#include <sys/dlpi.h>
		    ],
		    [int i = DL_PROMISC_PHYS;],
		    ac_cv_sys_dlpi_usable=yes,
		    ac_cv_sys_dlpi_usable=no))
	AC_MSG_RESULT($ac_cv_sys_dlpi_usable)
	if test $ac_cv_sys_dlpi_usable = no ; then
		AC_MSG_ERROR(<sys/dlpi.h> is not usable on this system; it probably has a non-standard DLPI)
	fi

	#
	# Check to see if Solaris has the dl_passive_req_t struct defined
	# in <sys/dlpi.h>.
	# This check is for DLPI support for passive modes.
	# See dlpi(7P) for more details.
	#
	AC_CHECK_TYPES(dl_passive_req_t,,,
	    [
		#include <sys/types.h>
		#include <sys/dlpi.h>
	    ])
	;;

linux)
	#
	# Do we have the wireless extensions?
	#
	AC_CHECK_HEADERS(linux/wireless.h, [], [],
	[
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/types.h>
	])

	#
	# Do we have libnl?
	#
	AC_ARG_WITH(libnl,
	AC_HELP_STRING([--without-libnl],[disable libnl support @<:@default=yes, on Linux, if present@:>@]),
		with_libnl=$withval,with_libnl=if_available)

	if test x$with_libnl != xno ; then
		have_any_nl="no"

                incdir=-I/usr/include/libnl3
                libnldir=
                case "$with_libnl" in

                yes|if_available)
                  ;;

                *)
                  if test -d $withval; then
                    libnldir=-L${withval}/lib/.libs
                    incdir=-I${withval}/include
                  fi
                  ;;
                esac

		#
		# Try libnl 3.x first.
		#
		AC_CHECK_LIB(nl-3, nl_socket_alloc,
		[
			#
			# Yes, we have libnl 3.x.
			#
			LIBS="${libnldir} -lnl-genl-3 -lnl-3 $LIBS"
			AC_DEFINE(HAVE_LIBNL,1,[if libnl exists])
			AC_DEFINE(HAVE_LIBNL_3_x,1,[if libnl exists and is version 3.x])
			AC_DEFINE(HAVE_LIBNL_NLE,1,[libnl has NLE_FAILURE])
			AC_DEFINE(HAVE_LIBNL_SOCKETS,1,[libnl has new-style socket api])
			V_INCLS="$V_INCLS ${incdir}"
			have_any_nl="yes"
		],[], ${incdir} ${libnldir} -lnl-genl-3 -lnl-3 )

		if test x$have_any_nl = xno ; then
			#
			# Try libnl 2.x
			#
			AC_CHECK_LIB(nl, nl_socket_alloc,
			[
				#
				# Yes, we have libnl 2.x.
				#
				LIBS="${libnldir} -lnl-genl -lnl $LIBS"
				AC_DEFINE(HAVE_LIBNL,1,[if libnl exists])
				AC_DEFINE(HAVE_LIBNL_2_x,1,[if libnl exists and is version 2.x])
				AC_DEFINE(HAVE_LIBNL_NLE,1,[libnl has NLE_FAILURE])
				AC_DEFINE(HAVE_LIBNL_SOCKETS,1,[libnl has new-style socket api])
				have_any_nl="yes"
			])
		fi

		if test x$have_any_nl = xno ; then
			#
			# No, we don't; do we have libnl 1.x?
			#
			AC_CHECK_LIB(nl, nl_handle_alloc,
			[
				#
				# Yes.
				#
				LIBS="${libnldir} -lnl $LIBS"
				AC_DEFINE(HAVE_LIBNL,1,[if libnl exists])
				have_any_nl="yes"
			])
		fi

		if test x$have_any_nl = xno ; then
			#
			# No, we don't have libnl at all.
			#
			if test x$with_libnl = xyes ; then
				AC_MSG_ERROR([libnl support requested but libnl not found])
			fi
		fi
	fi

	AC_CHECK_HEADERS(linux/ethtool.h,,,
	    [
AC_INCLUDES_DEFAULT
#include <linux/types.h>
	    ])

	#
	# Check to see if struct tpacket_stats is defined in
	# <linux/if_packet.h>.  If so, then pcap-linux.c can use this
	# to report proper statistics.
	#
	# -Scott Barron
	#
	AC_CHECK_TYPES(struct tpacket_stats,,,
	    [
		#include <linux/if_packet.h>
	    ])

	#
	# Check to see if the tpacket_auxdata struct has a tp_vlan_tci member.
	#
	# NOTE: any failure means we conclude that it doesn't have that
	# member, so if we don't have tpacket_auxdata, we conclude it
	# doesn't have that member (which is OK, as either we won't be
	# using code that would use that member, or we wouldn't compile
	# in any case).
	AC_CHECK_MEMBERS([struct tpacket_auxdata.tp_vlan_tci],,,
	    [
		#include <sys/types.h>
		#include <linux/if_packet.h>
	    ])
	;;

bpf)
	#
	# Check whether we have the *BSD-style ioctls.
	#
	AC_CHECK_HEADERS(net/if_media.h)

	#
	# Check whether we have struct BPF_TIMEVAL.
	#
	AC_CHECK_TYPES(struct BPF_TIMEVAL,,,
	    [
		#include <sys/types.h>
		#include <sys/ioctl.h>
		#ifdef HAVE_SYS_IOCCOM_H
		#include <sys/ioccom.h>
		#endif
		#include <net/bpf.h>
	    ])
	;;

dag)
	#
	# --with-pcap=dag is the only way to get here, and it means
	# "DAG support but nothing else"
	#
	V_DEFS="$V_DEFS -DDAG_ONLY"
	xxx_only=yes
	;;

septel)
	#
	# --with-pcap=septel is the only way to get here, and it means
	# "Septel support but nothing else"
	#
	V_DEFS="$V_DEFS -DSEPTEL_ONLY"
	xxx_only=yes
	;;

snf)
	#
	# --with-pcap=snf is the only way to get here, and it means
	# "SNF support but nothing else"
	#
	V_DEFS="$V_DEFS -DSNF_ONLY"
	xxx_only=yes
	;;

null)
	;;

*)
	AC_MSG_ERROR($V_PCAP is not a valid pcap type)
	;;
esac

dnl
dnl Now figure out how we get a list of interfaces and addresses,
dnl if we support capturing.  Don't bother if we don't support
dnl capturing.
dnl
if test "$V_PCAP" != null
then
	AC_CHECK_FUNC(getifaddrs,[
		#
		# We have "getifaddrs()"; make sure we have <ifaddrs.h>
		# as well, just in case some platform is really weird.
		#
		AC_CHECK_HEADER(ifaddrs.h,[
		    #
		    # We have the header, so we use "getifaddrs()" to
		    # get the list of interfaces.
		    #
		    V_FINDALLDEVS=fad-getad.c
		],[
		    #
		    # We don't have the header - give up.
		    # XXX - we could also fall back on some other
		    # mechanism, but, for now, this'll catch this
		    # problem so that we can at least try to figure
		    # out something to do on systems with "getifaddrs()"
		    # but without "ifaddrs.h", if there is something
		    # we can do on those systems.
		    #
		    AC_MSG_ERROR([Your system has getifaddrs() but doesn't have a usable <ifaddrs.h>.])
		])
	],[
		#
		# Well, we don't have "getifaddrs()", at least not with the
		# libraries with which we've decided we need to link
		# libpcap with, so we have to use some other mechanism.
		#
		# Note that this may happen on Solaris, which has
		# getifaddrs(), but in -lsocket, not in -lxnet, so we
		# won't find it if we link with -lxnet, which we want
		# to do for other reasons.
		#
		# For now, we use either the SIOCGIFCONF ioctl or the
		# SIOCGLIFCONF ioctl, preferring the latter if we have
		# it; the latter is a Solarisism that first appeared
		# in Solaris 8.  (Solaris's getifaddrs() appears to
		# be built atop SIOCGLIFCONF; using it directly
		# avoids a not-all-that-useful middleman.)
		#
		AC_MSG_CHECKING(whether we have SIOCGLIFCONF)
		AC_CACHE_VAL(ac_cv_lbl_have_siocglifconf,
		    AC_TRY_COMPILE(
			[#include <sys/param.h>
			#include <sys/file.h>
			#include <sys/ioctl.h>
			#include <sys/socket.h>
			#include <sys/sockio.h>],
			[ioctl(0, SIOCGLIFCONF, (char *)0);],
			ac_cv_lbl_have_siocglifconf=yes,
			ac_cv_lbl_have_siocglifconf=no))
		AC_MSG_RESULT($ac_cv_lbl_have_siocglifconf)
		if test $ac_cv_lbl_have_siocglifconf = yes ; then
			V_FINDALLDEVS=fad-glifc.c
		else
			V_FINDALLDEVS=fad-gifc.c
		fi
	])
fi
])

dnl check for hardware timestamp support
case "$host_os" in
linux*)
	AC_CHECK_HEADERS([linux/net_tstamp.h])
	;;
*)
	AC_MSG_NOTICE(no hardware timestamp support implemented for $host_os)
	;;
esac

AC_ARG_ENABLE([packet-ring],
[AC_HELP_STRING([--enable-packet-ring],[enable packet ring support on Linux @<:@default=yes@:>@])],
,enable_packet_ring=yes)

if test "x$enable_packet_ring" != "xno" ; then
	AC_DEFINE(PCAP_SUPPORT_PACKET_RING, 1, [use packet ring capture support on Linux if available])
	AC_SUBST(PCAP_SUPPORT_PACKET_RING)
fi

#
# Check for socklen_t.
#
AC_CHECK_TYPES(socklen_t,,,
    [
	#include <sys/types.h>
	#include <sys/socket.h>
    ])

AC_ARG_ENABLE(ipv6,
AC_HELP_STRING([--enable-ipv6],[build IPv6-capable version @<:@default=yes@:>@]),
    [],
    [enable_ipv6=yes])
if test "$enable_ipv6" != "no"; then
	#
	# We've already made sure we have getaddrinfo above in
	# AC_LBL_LIBRARY_NET.
	#
	AC_DEFINE(INET6,1,[IPv6])
fi

# Check for Endace DAG card support.
AC_ARG_WITH([dag],
AC_HELP_STRING([--with-dag@<:@=DIR@:>@],[include Endace DAG support (located in directory DIR, if supplied).  @<:@default=yes, if present@:>@]),
[
	if test "$withval" = no
	then
		# User doesn't want DAG support.
		want_dag=no
	elif test "$withval" = yes
	then
		# User wants DAG support but hasn't specified a directory.
		want_dag=yes
	else
		# User wants DAG support and has specified a directory, so use the provided value.
		want_dag=yes
		dag_root=$withval
	fi
],[
	if test "$V_PCAP" = dag; then
		# User requested DAG-only libpcap, so we'd better have
		# the DAG API.
		want_dag=yes
	elif test "xxx_only" = yes; then
		# User requested something-else-only pcap, so they don't
		# want DAG support.
		want_dag=no
	else
		#
		# Use DAG API if present, otherwise don't
		#
		want_dag=ifpresent
	fi
])

AC_ARG_WITH([dag-includes],
AC_HELP_STRING([--with-dag-includes=IDIR],[Endace DAG include directory, if not DIR/include]),
[
	# User wants DAG support and has specified a header directory, so use the provided value.
	want_dag=yes
	dag_include_dir=$withval
],[])

AC_ARG_WITH([dag-libraries],
AC_HELP_STRING([--with-dag-libraries=LDIR],[Endace DAG library directory, if not DIR/lib]),
[
	# User wants DAG support and has specified a library directory, so use the provided value.
	want_dag=yes
	dag_lib_dir=$withval
],[])

if test "$want_dag" != no; then

	# If necessary, set default paths for DAG API headers and libraries.
	if test -z "$dag_root"; then
		dag_root=/usr/local
	fi

	if test -z "$dag_include_dir"; then
		dag_include_dir="$dag_root/include"
	fi

	if test -z "$dag_lib_dir"; then
		dag_lib_dir="$dag_root/lib"
	fi

	V_INCLS="$V_INCLS -I$dag_include_dir"

	AC_CHECK_HEADERS([dagapi.h])

	if test "$ac_cv_header_dagapi_h" = yes; then

		if test $V_PCAP != dag ; then
			 SSRC="$SSRC pcap-dag.c"
		fi

		# Check for various DAG API functions.
		# Don't need to save and restore LIBS to prevent -ldag being
		# included if there's a found-action (arg 3).
		saved_ldflags=$LDFLAGS
		LDFLAGS="-L$dag_lib_dir"
		AC_CHECK_LIB([dag], [dag_attach_stream],
		    [],
		    [AC_MSG_ERROR(DAG library lacks streams support)])
		AC_CHECK_LIB([dag], [dag_attach_stream64], [dag_large_streams="1"], [dag_large_streams="0"])
		AC_CHECK_LIB([dag],[dag_get_erf_types], [
			AC_DEFINE(HAVE_DAG_GET_ERF_TYPES, 1, [define if you have dag_get_erf_types()])])
		AC_CHECK_LIB([dag],[dag_get_stream_erf_types], [
			AC_DEFINE(HAVE_DAG_GET_STREAM_ERF_TYPES, 1, [define if you have dag_get_stream_erf_types()])])

		LDFLAGS=$saved_ldflags

		#
		# We assume that if we have libdag we have libdagconf,
		# as they're installed at the same time from the same
		# package.
		#
		LIBS="$LIBS -ldag -ldagconf"
		LDFLAGS="$LDFLAGS -L$dag_lib_dir"

		if test "$dag_large_streams" = 1; then
			AC_DEFINE(HAVE_DAG_LARGE_STREAMS_API, 1, [define if you have large streams capable DAG API])
			AC_CHECK_LIB([vdag],[vdag_set_device_info], [ac_dag_have_vdag="1"], [ac_dag_have_vdag="0"])
			if test "$ac_dag_have_vdag" = 1; then
				AC_DEFINE(HAVE_DAG_VDAG, 1, [define if you have vdag_set_device_info()])
				if test "$ac_lbl_have_pthreads" != "found"; then
					AC_MSG_ERROR([DAG requires pthreads, but we didn't find them])
				fi
				LIBS="$LIBS $PTHREAD_LIBS"
			fi
		fi

		AC_DEFINE(HAVE_DAG_API, 1, [define if you have the DAG API])
	else

		if test "$V_PCAP" = dag; then
			# User requested "dag" capture type but we couldn't
			# find the DAG API support.
			AC_MSG_ERROR([DAG support requested with --with-pcap=dag, but the DAG headers weren't found at $dag_include_dir: make sure the DAG support is installed, specify a different path or paths if necessary, or don't request DAG support])
		fi

		if test "$want_dag" = yes; then
	        	# User wanted DAG support but we couldn't find it.
			AC_MSG_ERROR([DAG support requested with --with-dag, but the DAG headers weren't found at $dag_include_dir: make sure the DAG support is installed, specify a different path or paths if necessary, or don't request DAG support])
		fi
	fi
fi

AC_ARG_WITH(septel,
AC_HELP_STRING([--with-septel@<:@=DIR@:>@],[include Septel support (located in directory DIR, if supplied).  @<:@default=yes, if present@:>@]),
[
	if test "$withval" = no
	then
		want_septel=no
	elif test "$withval" = yes
	then
		want_septel=yes
		septel_root=
	else
		want_septel=yes
		septel_root=$withval
	fi
],[
	if test "$V_PCAP" = septel; then
		# User requested Septel-only libpcap, so we'd better have
		# the Septel API.
		want_septel=yes
	elif test "xxx_only" = yes; then
		# User requested something-else-only pcap, so they don't
		# want Septel support.
		want_septel=no
	else
		#
		# Use Septel API if present, otherwise don't
		#
		want_septel=ifpresent
	fi
])

ac_cv_lbl_septel_api=no
if test "$with_septel" != no; then

	AC_MSG_CHECKING([whether we have Septel API headers])

	# If necessary, set default paths for Septel API headers and libraries.
	if test -z "$septel_root"; then
		septel_root=$srcdir/../septel
	fi

	septel_tools_dir="$septel_root"
	septel_include_dir="$septel_root/INC"

	if test -r "$septel_include_dir/msg.h"; then
		ac_cv_lbl_septel_api=yes
	fi

	if test "$ac_cv_lbl_septel_api" = yes; then
		AC_MSG_RESULT([yes ($septel_include_dir)])

		V_INCLS="$V_INCLS -I$septel_include_dir"
		ADDLOBJS="$ADDLOBJS $septel_tools_dir/asciibin.o $septel_tools_dir/bit2byte.o $septel_tools_dir/confirm.o $septel_tools_dir/fmtmsg.o $septel_tools_dir/gct_unix.o $septel_tools_dir/hqueue.o $septel_tools_dir/ident.o $septel_tools_dir/mem.o $septel_tools_dir/pack.o $septel_tools_dir/parse.o $septel_tools_dir/pool.o $septel_tools_dir/sdlsig.o $septel_tools_dir/strtonum.o $septel_tools_dir/timer.o $septel_tools_dir/trace.o"
		ADDLARCHIVEOBJS="$ADDLARCHIVEOBJS $septel_tools_dir/asciibin.o $septel_tools_dir/bit2byte.o $septel_tools_dir/confirm.o $septel_tools_dir/fmtmsg.o $septel_tools_dir/gct_unix.o $septel_tools_dir/hqueue.o $septel_tools_dir/ident.o $septel_tools_dir/mem.o $septel_tools_dir/pack.o $septel_tools_dir/parse.o $septel_tools_dir/pool.o $septel_tools_dir/sdlsig.o $septel_tools_dir/strtonum.o $septel_tools_dir/timer.o $septel_tools_dir/trace.o"

		if test "$V_PCAP" != septel ; then
			 SSRC="$SSRC pcap-septel.c"
		fi

		AC_DEFINE(HAVE_SEPTEL_API, 1, [define if you have the Septel API])
	else
		AC_MSG_RESULT(no)

		if test "$V_PCAP" = septel; then
			# User requested "septel" capture type but
			# we couldn't find the Septel API support.
			AC_MSG_ERROR([Septel support requested with --with-pcap=septel, but the Septel headers weren't found at $septel_include_dir: make sure the Septel support is installed, specify a different path or paths if necessary, or don't request Septel support])
		fi

		if test "$want_septel" = yes; then
	        	# User wanted Septel support but we couldn't find it.
			AC_MSG_ERROR([Septel support requested with --with-septel, but the Septel headers weren't found at $septel_include_dir: make sure the Septel support is installed, specify a different path or paths if necessary, or don't request Septel support])
		fi
	fi
fi

# Check for Myricom SNF support.
AC_ARG_WITH([snf],
AC_HELP_STRING([--with-snf@<:@=DIR@:>@],[include Myricom SNF support (located in directory DIR, if supplied).  @<:@default=yes, if present@:>@]),
[
	if test "$withval" = no
	then
		# User explicitly doesn't want SNF
		want_snf=no
	elif test "$withval" = yes
	then
		# User wants SNF support but hasn't specified a directory.
		want_snf=yes
	else
		# User wants SNF support with a specified directory.
		want_snf=yes
		snf_root=$withval
	fi
],[
	if test "$V_PCAP" = snf; then
		# User requested Sniffer-only libpcap, so we'd better have
		# the Sniffer API.
		want_snf=yes
	elif test "xxx_only" = yes; then
		# User requested something-else-only pcap, so they don't
		# want SNF support.
		want_snf=no
	else
		#
		# Use Sniffer API if present, otherwise don't
		#
		want_snf=ifpresent
	fi
])

AC_ARG_WITH([snf-includes],
AC_HELP_STRING([--with-snf-includes=IDIR],[Myricom SNF include directory, if not DIR/include]),
[
	# User wants SNF with specific header directory
	want_snf=yes
	snf_include_dir=$withval
],[])

AC_ARG_WITH([snf-libraries],
AC_HELP_STRING([--with-snf-libraries=LDIR],[Myricom SNF library directory, if not DIR/lib]),
[
	# User wants SNF with specific lib directory
	want_snf=yes
	snf_lib_dir=$withval
],[])

ac_cv_lbl_snf_api=no
if test "$with_snf" != no; then

	AC_MSG_CHECKING(whether we have Myricom Sniffer API)

	# If necessary, set default paths for Sniffer headers and libraries.
	if test -z "$snf_root"; then
		snf_root=/opt/snf
	fi

	if test -z "$snf_include_dir"; then
		snf_include_dir="$snf_root/include"
	fi

	if test -z "$snf_lib_dir"; then
		snf_lib_dir="$snf_root/lib"
	fi

	if test -f "$snf_include_dir/snf.h"; then
		# We found a header; make sure we can link with the library
		saved_ldflags=$LDFLAGS
		LDFLAGS="$LDFLAGS -L$snf_lib_dir"
		AC_CHECK_LIB([snf], [snf_init], [ac_cv_lbl_snf_api="yes"])
		LDFLAGS="$saved_ldflags"
		if test "$ac_cv_lbl_snf_api" = no; then
			AC_MSG_ERROR(SNF API cannot correctly be linked; check config.log)
		fi
	fi

	if test "$ac_cv_lbl_snf_api" = yes; then
		AC_MSG_RESULT([yes ($snf_root)])

		V_INCLS="$V_INCLS -I$snf_include_dir"
		LIBS="$LIBS -lsnf"
		LDFLAGS="$LDFLAGS -L$snf_lib_dir"

		if test "$V_PCAP" != snf ; then
			SSRC="$SSRC pcap-snf.c"
		fi

		AC_DEFINE(HAVE_SNF_API, 1, [define if you have the Myricom SNF API])
	else
		AC_MSG_RESULT(no)

		if test "$want_snf" = yes; then
			# User requested "snf" capture type but
			# we couldn't find the Sniffer API support.
			AC_MSG_ERROR([Myricom Sniffer support requested with --with-pcap=snf, but the Sniffer headers weren't found at $snf_include_dir: make sure the Sniffer support is installed, specify a different path or paths if necessary, or don't request Sniffer support])
		fi

		if test "$want_snf" = yes; then
			AC_MSG_ERROR([Myricom Sniffer support requested with --with-snf, but the Sniffer headers weren't found at $snf_include_dir: make sure the Sniffer support is installed, specify a different path or paths if necessary, or don't request Sniffer support])
		fi
	fi
fi

# Check for Riverbed TurboCap support.
AC_ARG_WITH([turbocap],
AC_HELP_STRING([--with-turbocap@<:@=DIR@:>@],[include Riverbed TurboCap support (located in directory DIR, if supplied).  @<:@default=yes, if present@:>@]),
[
	if test "$withval" = no
	then
		# User explicitly doesn't want TurboCap
		want_turbocap=no
	elif test "$withval" = yes
	then
		# User wants TurboCap support but hasn't specified a directory.
		want_turbocap=yes
	else
		# User wants TurboCap support with a specified directory.
		want_turbocap=yes
		turbocap_root=$withval
	fi
],[
	if test "xxx_only" = yes; then
		# User requested something-else-only pcap, so they don't
		# want TurboCap support.
		want_turbocap=no
	else
		#
		# Use TurboCap API if present, otherwise don't
		#
		want_turbocap=ifpresent
	fi
])

ac_cv_lbl_turbocap_api=no
if test "$want_turbocap" != no; then

	AC_MSG_CHECKING(whether TurboCap is supported)

	save_CFLAGS="$CFLAGS"
	save_LIBS="$LIBS"
	if test ! -z "$turbocap_root"; then
		TURBOCAP_CFLAGS="-I$turbocap_root/include"
		TURBOCAP_LIBS="-L$turbocap_root/lib"
		CFLAGS="$CFLAGS $TURBOCAP_CFLAGS"
	fi

	AC_TRY_COMPILE(
	[
	    #include <TcApi.h>
	],
	[
	    TC_INSTANCE a; TC_PORT b; TC_BOARD c;
	    TC_INSTANCE i;
	    (void)TcInstanceCreateByName("foo", &i);
	],
	ac_cv_lbl_turbocap_api=yes)

	CFLAGS="$save_CFLAGS"
	if test $ac_cv_lbl_turbocap_api = yes; then
		AC_MSG_RESULT(yes)

		SSRC="$SSRC pcap-tc.c"
		V_INCLS="$V_INCLS $TURBOCAP_CFLAGS"
		LIBS="$LIBS $TURBOCAP_LIBS -lTcApi -lpthread -lstdc++"

		AC_DEFINE(HAVE_TC_API, 1, [define if you have the TurboCap API])
	else
		AC_MSG_RESULT(no)

		if test "$want_turbocap" = yes; then
	        	# User wanted Turbo support but we couldn't find it.
			AC_MSG_ERROR([TurboCap support requested with --with-turbocap, but the TurboCap headers weren't found: make sure the TurboCap support is installed or don't request TurboCap support])
		fi
	fi
fi

dnl
dnl Allow the user to enable remote capture.
dnl It's off by default, as that increases the attack surface of
dnl libpcap, exposing it to malicious servers.
dnl
AC_MSG_CHECKING([whether to enable remote packet capture])
AC_ARG_ENABLE(remote,
[  --enable-remote         enable remote packet capture @<:@default=no@:>@
  --disable-remote        disable remote packet capture],,
   enableval=no)
case "$enableval" in
yes)	AC_MSG_RESULT(yes)
	AC_WARN([Remote packet capture may expose libpcap-based applications])
	AC_WARN([to attacks by malicious remote capture servers!])
	#
	# rpcapd requires pthreads on UN*X.
	#
	if test "$ac_lbl_have_pthreads" != "found"; then
		AC_MSG_ERROR([rpcapd requires pthreads, but we didn't find them])
	fi
	#
	# It also requires crypt().
	# Do we have it in the system libraries?
	#
	AC_CHECK_FUNC(crypt,,
	    [
		#
		# No.  Do we have it in -lcrypt?
		#
		AC_CHECK_LIB(crypt, crypt,
		    [
			#
			# Yes; add -lcrypt to the libraries for rpcapd.
			#
			RPCAPD_LIBS="$RPCAPD_LIBS -lcrypt"
		    ],
		    [
			AC_MSG_ERROR([rpcapd requires crypt(), but we didn't find it])
		    ])
	    ])

	#
	# OK, we have crypt().  Do we have getspnam()?
	#
	AC_CHECK_FUNCS(getspnam)

	#
	# Check for various members of struct msghdr.
	#
	AC_CHECK_MEMBERS([struct msghdr.msg_control],,,
	    [
		#include "ftmacros.h"
		#include <sys/socket.h>
	    ])
	AC_CHECK_MEMBERS([struct msghdr.msg_flags],,,
	    [
		#include "ftmacros.h"
		#include <sys/socket.h>
	    ])

	AC_DEFINE(ENABLE_REMOTE,,
	    [Define to 1 if remote packet capture is to be supported])
	SSRC="$SSRC pcap-new.c pcap-rpcap.c rpcap-protocol.c sockutils.c"
	BUILD_RPCAPD=build-rpcapd
	INSTALL_RPCAPD=install-rpcapd
	;;
*)	AC_MSG_RESULT(no)
	;;
esac

AC_MSG_CHECKING(whether to build optimizer debugging code)
AC_ARG_ENABLE(optimizer-dbg,
AC_HELP_STRING([--enable-optimizer-dbg],[build optimizer debugging code]))
if test "$enable_optimizer_dbg" = "yes"; then
	AC_DEFINE(BDEBUG,1,[Enable optimizer debugging])
fi
AC_MSG_RESULT(${enable_optimizer_dbg-no})

AC_MSG_CHECKING(whether to build parser debugging code)
AC_ARG_ENABLE(yydebug,
AC_HELP_STRING([--enable-yydebug],[build parser debugging code]))
if test "$enable_yydebug" = "yes"; then
	AC_DEFINE(YYDEBUG,1,[Enable parser debugging])
fi
AC_MSG_RESULT(${enable_yydebug-no})

#
# Look for {f}lex.
#
AC_PROG_LEX
if test "$LEX" = ":"; then
	AC_MSG_ERROR([Neither flex nor lex was found.])
fi

#
# Make sure {f}lex supports the -P, --header-file, and --nounput flags
# and supports processing our scanner.l.
#
AC_CACHE_CHECK([for capable lex], tcpdump_cv_capable_lex,
	if $LEX -P pcap_ --header-file=/dev/null --nounput -t $srcdir/scanner.l > /dev/null 2>&1; then
	    tcpdump_cv_capable_lex=yes
	else
	    tcpdump_cv_capable_lex=insufficient
	fi)
if test $tcpdump_cv_capable_lex = insufficient ; then
	AC_MSG_ERROR([$LEX is insufficient to compile libpcap.
 libpcap requires Flex 2.5.31 or later, or a compatible version of lex.])
fi

#
# Look for yacc/bison/byacc.
#
AC_PROG_YACC

#
# Make sure it supports the -p flag and supports processing our
# grammar.y.
#
AC_CACHE_CHECK([for capable yacc/bison], tcpdump_cv_capable_yacc,
	if $YACC -p pcap_ -o /dev/null $srcdir/grammar.y >/dev/null 2>&1; then
	    tcpdump_cv_capable_yacc=yes
	else
	    tcpdump_cv_capable_yacc=insufficient
	fi)
if test $tcpdump_cv_capable_yacc = insufficient ; then
	AC_MSG_ERROR([$YACC is insufficient to compile libpcap.
 libpcap requires Bison, Berkeley YACC, or another YACC compatible with them.])
fi

#
# Do various checks for various OSes and versions of those OSes.
#
# Assume, by default, no support for shared libraries and V7/BSD
# convention for man pages (file formats in section 5, miscellaneous
# info in section 7, administrative commands and daemons in section 8).
# Individual cases can override this.
#
DYEXT="none"
MAN_FILE_FORMATS=5
MAN_MISC_INFO=7
MAN_ADMIN_COMMANDS=8
case "$host_os" in

aix*)
	dnl Workaround to enable certain features
	AC_DEFINE(_SUN,1,[define on AIX to get certain functions])

	#
	# AIX makes it fun to build shared and static libraries,
	# because they're *both* ".a" archive libraries.  We
	# build the static library for the benefit of the traditional
	# scheme of building libpcap and tcpdump in subdirectories of
	# the same directory, with tcpdump statically linked with the
	# libpcap in question, but we also build a shared library as
	# "libpcap.shareda" and install *it*, rather than the static
	# library, as "libpcap.a".
	#
	DYEXT="shareda"

	case "$V_PCAP" in

	dlpi)
		#
		# If we're using DLPI, applications will need to
		# use /lib/pse.exp if present, as we use the
		# STREAMS routines.
		#
		pseexe="/lib/pse.exp"
		AC_MSG_CHECKING(for $pseexe)
		if test -f $pseexe ; then
			AC_MSG_RESULT(yes)
			LIBS="-I:$pseexe"
		fi
		;;

	bpf)
		#
		# If we're using BPF, we need "-lodm" and "-lcfg", as
		# we use them to load the BPF module.
		#
		LIBS="-lodm -lcfg"
		;;
	esac
	;;

darwin*)
	DYEXT="dylib"
	V_CCOPT="$V_CCOPT -fno-common"
	AC_ARG_ENABLE(universal,
	AC_HELP_STRING([--disable-universal],[don't build universal on macOS]))
	if test "$enable_universal" != "no"; then
		case "$host_os" in

		darwin[0-7].*)
			#
			# Pre-Tiger.  Build only for 32-bit PowerPC; no
			# need for any special compiler or linker flags.
			#
			;;

		darwin8.[0123]*)
			#
			# Tiger, prior to Intel support.  Build for 32-bit
			# PowerPC and 64-bit PowerPC, with 32-bit PowerPC
			# first.  (I'm guessing that's what Apple does.)
			#
			V_CCOPT="$V_CCOPT -arch ppc -arch ppc64"
			LDFLAGS="$LDFLAGS -arch ppc -arch ppc64"
			;;

		darwin8.[456]*)
			#
			# Tiger, subsequent to Intel support but prior to
			# x86-64 support.  Build for 32-bit PowerPC, 64-bit
			# PowerPC, and x86, with 32-bit PowerPC first.
			# (I'm guessing that's what Apple does.)
			#
			V_CCOPT="$V_CCOPT -arch ppc -arch ppc64 -arch i386"
			LDFLAGS="$LDFLAGS -arch ppc -arch ppc64 -arch i386"
			;;

		darwin8.*)
			#
			# All other Tiger, so subsequent to x86-64
			# support. Build for 32-bit PowerPC, 64-bit
			# PowerPC, x86, and x86-64, and with 32-bit PowerPC
			# first.  (I'm guessing that's what Apple does.)
			#
			V_CCOPT="$V_CCOPT -arch ppc -arch ppc64 -arch i386 -arch x86_64"
			LDFLAGS="$LDFLAGS -arch ppc -arch ppc64 -arch i386 -arch x86_64"
			;;

		darwin9.*)
			#
			# Leopard.  Build for 32-bit PowerPC, 64-bit
			# PowerPC, x86, and x86-64, with 32-bit PowerPC
			# first.  (That's what Apple does.)
			#
			V_CCOPT="$V_CCOPT -arch ppc -arch ppc64 -arch i386 -arch x86_64"
			LDFLAGS="$LDFLAGS -arch ppc -arch ppc64 -arch i386 -arch x86_64"
			;;

		darwin10.*)
			#
			# Snow Leopard.  Build for x86-64, x86, and
			# 32-bit PowerPC, with x86-64 first.  (That's
			# what Apple does, even though Snow Leopard
			# doesn't run on PPC, so PPC libpcap runs under
			# Rosetta, and Rosetta doesn't support BPF
			# ioctls, so PPC programs can't do live
			# captures.)
			#
			V_CCOPT="$V_CCOPT -arch x86_64 -arch i386 -arch ppc"
			LDFLAGS="$LDFLAGS -arch x86_64 -arch i386 -arch ppc"
			;;

		darwin*)
			#
			# Post-Snow Leopard.  Build for x86-64 and
			# x86, with x86-64 first.  (That's probably what
			# Apple does, given that Rosetta is gone.)
			# XXX - update if and when Apple drops support
			# for 32-bit x86 code.
			#
			V_CCOPT="$V_CCOPT -arch x86_64 -arch i386"
			LDFLAGS="$LDFLAGS -arch x86_64 -arch i386"
			;;
		esac
	fi
	;;

hpux9*)
	AC_DEFINE(HAVE_HPUX9,1,[on HP-UX 9.x])

	#
	# Use System V conventions for man pages.
	#
	MAN_ADMIN_COMMANDS=1m
	MAN_FILE_FORMATS=4
	MAN_MISC_INFO=5
	;;

hpux10.0*)

	#
	# Use System V conventions for man pages.
	#
	MAN_ADMIN_COMMANDS=1m
	MAN_FILE_FORMATS=4
	MAN_MISC_INFO=5
	;;

hpux10.1*)

	#
	# Use System V conventions for man pages.
	#
	MAN_ADMIN_COMMANDS=1m
	MAN_FILE_FORMATS=4
	MAN_MISC_INFO=5
	;;

hpux*)
	dnl HPUX 10.20 and above is similar to HPUX 9, but
	dnl not the same....
	dnl
	dnl XXX - DYEXT should be set to "sl" if this is building
	dnl for 32-bit PA-RISC, but should be left as "so" for
	dnl 64-bit PA-RISC or, I suspect, IA-64.
	AC_DEFINE(HAVE_HPUX10_20_OR_LATER,1,[on HP-UX 10.20 or later])
	if test "`uname -m`" = "ia64"; then
		DYEXT="so"
	else
		DYEXT="sl"
	fi

	#
	# "-b" builds a shared library; "+h" sets the soname.
	#
	SHLIB_OPT="-b"
	SONAME_OPT="+h"

	#
	# Use System V conventions for man pages.
	#
	MAN_FILE_FORMATS=4
	MAN_MISC_INFO=5
	;;

irix*)
	#
	# Use IRIX conventions for man pages; they're the same as the
	# System V conventions, except that they use section 8 for
	# administrative commands and daemons.
	#
	MAN_FILE_FORMATS=4
	MAN_MISC_INFO=5
	;;

linux*|freebsd*|netbsd*|openbsd*|dragonfly*|kfreebsd*|gnu*)
	DYEXT="so"

	#
	# Compiler assumed to be GCC; run-time linker may require a -R
	# flag.
	#
	if test "$libdir" != "/usr/lib"; then
		V_RFLAGS=-Wl,-R$libdir
	fi
	;;

osf*)
	DYEXT="so"

	#
	# DEC OSF/1, a/k/a Digial UNIX, a/k/a Tru64 UNIX.
	# Use Tru64 UNIX conventions for man pages; they're the same as
	# the System V conventions except that they use section 8 for
	# administrative commands and daemons.
	#
	MAN_FILE_FORMATS=4
	MAN_MISC_INFO=5
	;;

sinix*)
	AC_MSG_CHECKING(if SINIX compiler defines sinix)
	AC_CACHE_VAL(ac_cv_cc_sinix_defined,
		AC_TRY_COMPILE(
		    [],
		    [int i = sinix;],
		    ac_cv_cc_sinix_defined=yes,
		    ac_cv_cc_sinix_defined=no))
	    AC_MSG_RESULT($ac_cv_cc_sinix_defined)
	    if test $ac_cv_cc_sinix_defined = no ; then
		    AC_DEFINE(sinix,1,[on sinix])
	    fi
	;;

solaris*)
	AC_DEFINE(HAVE_SOLARIS,1,[On solaris])

	DYEXT="so"

	#
	# Make sure errno is thread-safe, in case we're called in
	# a multithreaded program.  We don't guarantee that two
	# threads can use the *same* pcap_t safely, but the
	# current version does guarantee that you can use different
	# pcap_t's in different threads, and even that pcap_compile()
	# is thread-safe (it wasn't thread-safe in some older versions).
	#
	V_CCOPT="$V_CCOPT -D_TS_ERRNO"

	case "`uname -r`" in

	5.12)
		;;

	*)
		#
		# Use System V conventions for man pages.
		#
		MAN_ADMIN_COMMANDS=1m
		MAN_FILE_FORMATS=4
		MAN_MISC_INFO=5
	esac
	;;
esac

AC_ARG_ENABLE(shared,
AC_HELP_STRING([--enable-shared],[build shared libraries @<:@default=yes, if support available@:>@]))
test "x$enable_shared" = "xno" && DYEXT="none"

AC_PROG_RANLIB
AC_CHECK_TOOL([AR], [ar])

AC_PROG_LN_S
AC_SUBST(LN_S)

AC_LBL_DEVEL(V_CCOPT)

#
# Check to see if the sockaddr struct has the 4.4 BSD sa_len member.
#
AC_CHECK_MEMBERS([struct sockaddr.sa_len],,,
    [
	#include <sys/types.h>
	#include <sys/socket.h>
    ])

#
# Check to see if there's a sockaddr_storage structure.
#
AC_CHECK_TYPES(struct sockaddr_storage,,,
    [
	#include <sys/types.h>
	#include <sys/socket.h>
    ])

#
# Check to see if the dl_hp_ppa_info_t struct has the HP-UX 11.00
# dl_module_id_1 member.
#
# NOTE: any failure means we conclude that it doesn't have that member,
# so if we don't have DLPI, don't have a <sys/dlpi_ext.h> header, or
# have one that doesn't declare a dl_hp_ppa_info_t type, we conclude
# it doesn't have that member (which is OK, as either we won't be
# using code that would use that member, or we wouldn't compile in
# any case).
#
AC_CHECK_MEMBERS([dl_hp_ppa_info_t.dl_module_id_1],,,
    [
	#include <sys/types.h>
	#include <sys/dlpi.h>
	#include <sys/dlpi_ext.h>
    ])

AC_LBL_UNALIGNED_ACCESS

AC_SUBST(V_CCOPT)
AC_SUBST(V_DEFS)
AC_SUBST(V_FINDALLDEVS)
AC_SUBST(V_INCLS)
AC_SUBST(V_LEX)
AC_SUBST(V_PCAP)
AC_SUBST(V_SHLIB_CCOPT)
AC_SUBST(V_SHLIB_CMD)
AC_SUBST(V_SHLIB_OPT)
AC_SUBST(V_SONAME_OPT)
AC_SUBST(V_RPATH_OPT)
AC_SUBST(V_YACC)
AC_SUBST(ADDLOBJS)
AC_SUBST(ADDLARCHIVEOBJS)
AC_SUBST(SSRC)
AC_SUBST(DYEXT)
AC_SUBST(MAN_FILE_FORMATS)
AC_SUBST(MAN_MISC_INFO)
AC_SUBST(MAN_ADMIN_COMMANDS)
AC_SUBST(PTHREAD_LIBS)
AC_SUBST(BUILD_RPCAPD)
AC_SUBST(INSTALL_RPCAPD)
AC_SUBST(RPCAPD_LIBS)
AC_SUBST(EXTRA_NETWORK_LIBS)

AC_ARG_ENABLE([usb],
[AC_HELP_STRING([--enable-usb],[enable USB capture support @<:@default=yes, if support available@:>@])],
    [],
    [enable_usb=yes])

if test "xxx_only" = yes; then
	# User requested something-else-only pcap, so they don't
	# want USB support.
	enable_usb=no
fi

if test "x$enable_usb" != "xno" ; then
   dnl check for USB sniffing support
   AC_MSG_CHECKING(for USB sniffing support)
   case "$host_os" in
   linux*)
	AC_DEFINE(PCAP_SUPPORT_USB, 1, [target host supports USB sniffing])
	USB_SRC=pcap-usb-linux.c
	AC_MSG_RESULT(yes)
	ac_usb_dev_name=`udevinfo -q name -p /sys/class/usb_device/usbmon 2>/dev/null`
	if test $? -ne 0 ; then
	  ac_usb_dev_name="usbmon"
	fi
	AC_DEFINE_UNQUOTED(LINUX_USB_MON_DEV, "/dev/$ac_usb_dev_name", [path for device for USB sniffing])
	AC_MSG_NOTICE(Device for USB sniffing is /dev/$ac_usb_dev_name)
	#
	# Do we have a version of <linux/compiler.h> available?
	# If so, we might need it for <linux/usbdevice_fs.h>.
	#
	AC_CHECK_HEADERS(linux/compiler.h)
	if test "$ac_cv_header_linux_compiler_h" = yes; then
	  #
	  # Yes - include it when testing for <linux/usbdevice_fs.h>.
	  #
	  AC_CHECK_HEADERS(linux/usbdevice_fs.h,,,[#include <linux/compiler.h>])
	else
	  AC_CHECK_HEADERS(linux/usbdevice_fs.h)
	fi
	if test "$ac_cv_header_linux_usbdevice_fs_h" = yes; then
	  #
	  # OK, does it define bRequestType?  Older versions of the kernel
	  # define fields with names like "requesttype, "request", and
	  # "value", rather than "bRequestType", "bRequest", and
	  # "wValue".
	  #
	  AC_CHECK_MEMBERS([struct usbdevfs_ctrltransfer.bRequestType],,,
	      [
		  AC_INCLUDES_DEFAULT
		  #ifdef HAVE_LINUX_COMPILER_H
		  #include <linux/compiler.h>
		  #endif
		  #include <linux/usbdevice_fs.h>
	      ])
	fi
	;;
    freebsd*)
	#
	# This just uses BPF in FreeBSD 8.4 and later; we don't need
	# to check for anything special for capturing.
	#
	AC_MSG_RESULT([yes, in FreeBSD 8.4 and later])
	;;

    *)
	AC_MSG_RESULT(no)
	;;
esac
fi
AC_SUBST(PCAP_SUPPORT_USB)
AC_SUBST(USB_SRC)

dnl check for netfilter sniffing support
if test "xxx_only" != yes; then
	AC_MSG_CHECKING(whether the platform could support netfilter sniffing)
	case "$host_os" in
	linux*)
		AC_MSG_RESULT(yes)
		#
		# Life's too short to deal with trying to get this to compile
		# if you don't get the right types defined with
		# __KERNEL_STRICT_NAMES getting defined by some other include.
		#
		# Check whether the includes Just Work.  If not, don't turn on
		# netfilter support.
		#
		AC_MSG_CHECKING(whether we can compile the netfilter support)
		AC_CACHE_VAL(ac_cv_netfilter_can_compile,
		  AC_TRY_COMPILE([
AC_INCLUDES_DEFAULT
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/types.h>

#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_log.h>
#include <linux/netfilter/nfnetlink_queue.h>],
		    [],
		    ac_cv_netfilter_can_compile=yes,
		    ac_cv_netfilter_can_compile=no))
		AC_MSG_RESULT($ac_cv_netfilter_can_compile)
		if test $ac_cv_netfilter_can_compile = yes ; then
		  AC_DEFINE(PCAP_SUPPORT_NETFILTER, 1,
		    [target host supports netfilter sniffing])
		  NETFILTER_SRC=pcap-netfilter-linux.c
		fi
		;;
	*)
		AC_MSG_RESULT(no)
		;;
	esac
fi
AC_SUBST(PCAP_SUPPORT_NETFILTER)
AC_SUBST(NETFILTER_SRC)

AC_ARG_ENABLE([netmap],
[AC_HELP_STRING([--enable-netmap],[enable netmap support @<:@default=yes, if support available@:>@])],
    [],
    [enable_netmap=yes])

if test "x$enable_netmap" != "xno" ; then
	#
	# Check whether net/netmap_user.h is usable if NETMAP_WITH_LIBS is
	# defined; it's not usable on DragonFly BSD 4.6 if NETMAP_WITH_LIBS
	# is defined, for example, as it includes a non-existent malloc.h
	# header.
	#
	AC_MSG_CHECKING(whether we can compile the netmap support)
	AC_CACHE_VAL(ac_cv_net_netmap_user_can_compile,
	  AC_TRY_COMPILE([
AC_INCLUDES_DEFAULT
#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>],
	    [],
	    ac_cv_net_netmap_user_can_compile=yes,
	    ac_cv_net_netmap_user_can_compile=no))
	AC_MSG_RESULT($ac_cv_net_netmap_user_can_compile)
	if test $ac_cv_net_netmap_user_can_compile = yes ; then
	  AC_DEFINE(PCAP_SUPPORT_NETMAP, 1,
	    [target host supports netmap])
	    NETMAP_SRC=pcap-netmap.c
	fi
	AC_SUBST(PCAP_SUPPORT_NETMAP)
	AC_SUBST(NETMAP_SRC)
fi


AC_ARG_ENABLE([bluetooth],
[AC_HELP_STRING([--enable-bluetooth],[enable Bluetooth support @<:@default=yes, if support available@:>@])],
    [],
    [enable_bluetooth=ifsupportavailable])

if test "xxx_only" = yes; then
	# User requested something-else-only pcap, so they don't
	# want Bluetooth support.
	enable_bluetooth=no
fi

if test "x$enable_bluetooth" != "xno" ; then
	dnl check for Bluetooth sniffing support
	case "$host_os" in
	linux*)
		AC_CHECK_HEADER(bluetooth/bluetooth.h,
		    [
			#
			# We have bluetooth.h, so we support Bluetooth
			# sniffing.
			#
			AC_DEFINE(PCAP_SUPPORT_BT, 1, [target host supports Bluetooth sniffing])
			BT_SRC=pcap-bt-linux.c
			AC_MSG_NOTICE(Bluetooth sniffing is supported)
			ac_lbl_bluetooth_available=yes

			#
			# OK, does struct sockaddr_hci have an hci_channel
			# member?
			#
			AC_CHECK_MEMBERS([struct sockaddr_hci.hci_channel],
			    [
				#
				# Yes; is HCI_CHANNEL_MONITOR defined?
				#
				AC_MSG_CHECKING(if HCI_CHANNEL_MONITOR is defined)
				AC_CACHE_VAL(ac_cv_lbl_hci_channel_monitor_is_defined,
				    AC_TRY_COMPILE(
					[
					    #include <bluetooth/bluetooth.h>
					    #include <bluetooth/hci.h>
					],
					[
					    u_int i = HCI_CHANNEL_MONITOR;
					],
					[
					    AC_MSG_RESULT(yes)
					    AC_DEFINE(PCAP_SUPPORT_BT_MONITOR,,
					      [target host supports Bluetooth Monitor])
					    BT_MONITOR_SRC=pcap-bt-monitor-linux.c
					],
					[
					    AC_MSG_RESULT(no)
					]))
			    ],,
			    [
				#include <bluetooth/bluetooth.h>
				#include <bluetooth/hci.h>
			    ])
		    ],
		    [
			#
			# We don't have bluetooth.h, so we don't support
			# Bluetooth sniffing.
			#
			if test "x$enable_bluetooth" = "xyes" ; then
				AC_MSG_ERROR(Bluetooth sniffing is not supported; install bluez-lib devel to enable it)
			else
				AC_MSG_NOTICE(Bluetooth sniffing is not supported; install bluez-lib devel to enable it)
			fi
		    ])
		;;
	*)
		if test "x$enable_bluetooth" = "xyes" ; then
			AC_MSG_ERROR(no Bluetooth sniffing support implemented for $host_os)
		else
			AC_MSG_NOTICE(no Bluetooth sniffing support implemented for $host_os)
		fi
		;;
	esac
	AC_SUBST(PCAP_SUPPORT_BT)
	AC_SUBST(BT_SRC)
	AC_SUBST(BT_MONITOR_SRC)
fi

AC_ARG_ENABLE([dbus],
[AC_HELP_STRING([--enable-dbus],[enable D-Bus capture support @<:@default=yes, if support available@:>@])],
    [],
    [enable_dbus=ifavailable])

if test "xxx_only" = yes; then
	# User requested something-else-only pcap, so they don't
	# want D-Bus support.
	enable_dbus=no
fi

if test "x$enable_dbus" != "xno"; then
	if test "x$enable_dbus" = "xyes"; then
		case "$host_os" in

		darwin*)
			#
			# We don't support D-Bus sniffing on macOS; see
			#
			# https://bugs.freedesktop.org/show_bug.cgi?id=74029
			#
			# The user requested it, so fail.
			#
			AC_MSG_ERROR([Due to freedesktop.org bug 74029, D-Bus capture support is not available on macOS])
		esac
	else
		case "$host_os" in

		darwin*)
			#
			# We don't support D-Bus sniffing on macOS; see
			#
			# https://bugs.freedesktop.org/show_bug.cgi?id=74029
			#
			# The user dind't explicitly request it, so just
			# silently refuse to enable it.
			#
			enable_dbus="no"
			;;
		esac
	fi
fi

if test "x$enable_dbus" != "xno"; then
	AC_CHECK_PROG([PKGCONFIG], [pkg-config], [pkg-config], [no])
	if test "x$PKGCONFIG" != "xno"; then
		AC_MSG_CHECKING([for D-Bus])
		if "$PKGCONFIG" dbus-1; then
			AC_MSG_RESULT([yes])
			DBUS_CFLAGS=`"$PKGCONFIG" --cflags dbus-1`
			DBUS_LIBS=`"$PKGCONFIG" --libs dbus-1`
			save_CFLAGS="$CFLAGS"
			save_LIBS="$LIBS"
			CFLAGS="$CFLAGS $DBUS_CFLAGS"
			LIBS="$LIBS $DBUS_LIBS"
			AC_MSG_CHECKING(whether the D-Bus library defines dbus_connection_read_write)
			AC_TRY_LINK(
			    [#include <string.h>

			     #include <time.h>
			     #include <sys/time.h>

			     #include <dbus/dbus.h>],
			    [return dbus_connection_read_write(NULL, 0);],
			    [
				AC_MSG_RESULT([yes])
				AC_DEFINE(PCAP_SUPPORT_DBUS, 1, [support D-Bus sniffing])
				DBUS_SRC=pcap-dbus.c
				V_INCLS="$V_INCLS $DBUS_CFLAGS"
			    ],
			    [
				AC_MSG_RESULT([no])
				if test "x$enable_dbus" = "xyes"; then
				    AC_MSG_ERROR([--enable-dbus was given, but the D-Bus library doesn't define dbus_connection_read_write()])
				fi
				LIBS="$save_LIBS"
			     ])
			CFLAGS="$save_CFLAGS"
		else
			AC_MSG_RESULT([no])
			if test "x$enable_dbus" = "xyes"; then
				AC_MSG_ERROR([--enable-dbus was given, but the dbus-1 package is not installed])
			fi
		fi
	fi
	AC_SUBST(PCAP_SUPPORT_DBUS)
	AC_SUBST(DBUS_SRC)
fi

AC_ARG_ENABLE([rdma],
[AC_HELP_STRING([--enable-rdma],[enable RDMA capture support @<:@default=yes, if support available@:>@])],
    [],
    [enable_rdmasniff=ifavailable])

if test "xxx_only" = yes; then
	# User requested something-else-only pcap, so they don't
	# want RDMA support.
	enable_rdmasniff=no
fi

if test "x$enable_rdmasniff" != "xno"; then
	AC_CHECK_LIB(ibverbs, ibv_get_device_list, [
		AC_CHECK_HEADER(infiniband/verbs.h, [
			#
			# ibv_create_flow may be defined as a static inline
			# function in infiniband/verbs.h, so we can't
			# use AC_CHECK_LIB.
			#
			# Too bad autoconf has no AC_SYMBOL_EXISTS()
			# macro that works like CMake's check_symbol_exists()
			# function, to check do a compile check like
			# this (they do a clever trick to avoid having
			# to know the function's signature).
			#
			AC_MSG_CHECKING(whether libibverbs defines ibv_create_flow)
			AC_TRY_LINK(
				[
					#include <infiniband/verbs.h>
				],
				[
					(void) ibv_create_flow((struct ibv_qp *) NULL,
							       (struct ibv_flow_attr *) NULL);
				],
				[
					AC_MSG_RESULT([yes])
					AC_DEFINE(PCAP_SUPPORT_RDMASNIFF, , [target host supports RDMA sniffing])
					RDMA_SRC=pcap-rdmasniff.c
					LIBS="-libverbs $LIBS"
				],
				[
					AC_MSG_RESULT([no])
				]
			)
		])
	])
	AC_SUBST(PCAP_SUPPORT_RDMASNIFF)
	AC_SUBST(RDMA_SRC)
fi

AC_PROG_INSTALL

AC_CONFIG_HEADER(config.h)

AC_OUTPUT_COMMANDS([if test -f .devel; then
	echo timestamp > stamp-h
	cat $srcdir/Makefile-devel-adds >> Makefile
	make depend
fi])
AC_OUTPUT(Makefile pcap-filter.manmisc pcap-linktype.manmisc
	pcap-tstamp.manmisc pcap-savefile.manfile pcap.3pcap
	pcap_compile.3pcap pcap_datalink.3pcap pcap_dump_open.3pcap
	pcap_get_tstamp_precision.3pcap pcap_list_datalinks.3pcap
	pcap_list_tstamp_types.3pcap pcap_open_dead.3pcap
	pcap_open_offline.3pcap pcap_set_tstamp_precision.3pcap
	pcap_set_tstamp_type.3pcap rpcapd/Makefile rpcapd/rpcapd.manadmin
	testprogs/Makefile)
exit 0
