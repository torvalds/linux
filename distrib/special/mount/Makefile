#	$OpenBSD: Makefile,v 1.3 2023/08/14 08:56:21 kn Exp $

PROG=	mount
SRCS=	mount.c
COPTS+=	-DSMALL
DPADD=	${LIBUTIL}
LDADD=	-lutil

.PATH:	${.CURDIR}/../../../sbin/mount
.include <bsd.prog.mk>
