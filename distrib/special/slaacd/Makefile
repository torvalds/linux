#	$OpenBSD: Makefile,v 1.4 2023/08/14 08:56:21 kn Exp $

PROG=	slaacd
COPTS+=	-DSMALL
.PATH:  ${.CURDIR}/../../../sbin/slaacd
SRCS=	engine.c frontend.c slaacd.c

YFLAGS=
LDADD+=	-levent -lutil
DPADD+= ${LIBEVENT} ${LIBUTIL}

.include <bsd.prog.mk>
