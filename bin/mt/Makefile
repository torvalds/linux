#	$OpenBSD: Makefile,v 1.9 2016/03/30 06:38:40 jmc Exp $

COPTS+=	-DRMT
PROG=	mt
SRCS=	mt.c mtrmt.c
DPADD=	${LIBUTIL}
LDADD=	-lutil

LINKS=	${BINDIR}/mt ${BINDIR}/eject

.include <bsd.prog.mk>
