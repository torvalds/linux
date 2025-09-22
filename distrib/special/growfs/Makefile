# $OpenBSD: Makefile,v 1.1 2017/11/19 12:11:54 job Exp $

PROG=	growfs
SRCS=	growfs.c

DPADD=	${LIBUTIL}
LDADD=	-lutil

.PATH:	${.CURDIR}/../../../sbin/growfs
.include <bsd.prog.mk>
