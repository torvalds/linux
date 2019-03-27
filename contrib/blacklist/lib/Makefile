# $NetBSD: Makefile,v 1.6 2016/01/05 13:07:46 christos Exp $

.include <bsd.own.mk>

USE_SHLIBDIR=   yes

CPPFLAGS+=-D_REENTRANT
LIBDPLIBS+=pthread ${NETBSDSRCDIR}/lib/libpthread
LIB=blacklist
SRCS=bl.c blacklist.c
MAN=libblacklist.3
MLINKS+=libblacklist.3 blacklist_open.3
MLINKS+=libblacklist.3 blacklist_close.3
MLINKS+=libblacklist.3 blacklist.3
MLINKS+=libblacklist.3 blacklist_r.3
MLINKS+=libblacklist.3 blacklist_sa.3
MLINKS+=libblacklist.3 blacklist_sa_r.3

.include <bsd.lib.mk>
