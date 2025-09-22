#	$OpenBSD: Makefile,v 1.13 2017/07/03 22:21:47 espie Exp $

PROG=	chio
SRCS=	chio.c parse.y
DPADD=	${LIBUTIL}
LDADD=	-lutil
MAN=	chio.1 chio.conf.5
YFLAGS=

.include <bsd.prog.mk>
