#	$OpenBSD: Makefile,v 1.11 2017/08/29 15:30:02 bluhm Exp $

PROG=	ps
SRCS=	keyword.c nlist.c print.c ps.c utf8.c
DPADD=	${LIBKVM}
LDADD=	-lkvm

.include <bsd.prog.mk>
