#	$OpenBSD: Makefile,v 1.5 2016/05/03 17:54:31 jca Exp $

PROG=	ifconfig
SRCS=   ifconfig.c
COPTS+=	-DSMALL

LDADD=	-lutil
DPADD=	${LIBUTIL}

.PATH:  ${.CURDIR}/../../../sbin/ifconfig

.include <bsd.prog.mk>
