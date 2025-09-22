#	$OpenBSD: Makefile,v 1.5 2023/08/14 08:56:21 kn Exp $

PROG=	route
SRCS=	route.c show.c

CFLAGS+=	-Wall -DSMALL

route.o tags: keywords.h

.PATH:  ${.CURDIR}/../../../sbin/route
.include <bsd.prog.mk>
