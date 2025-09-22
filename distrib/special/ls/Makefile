#	$OpenBSD: Makefile,v 1.2 2015/12/02 05:39:42 deraadt Exp $

PROG=	ls
SRCS=	cmp.c ls.c main.c print.c util.c utf8.c
DPADD= ${LIBUTIL}
LDADD= -lutil

.PATH:  ${.CURDIR}/../../../bin/ls
.include <bsd.prog.mk>
