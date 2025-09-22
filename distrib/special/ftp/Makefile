#	$OpenBSD: Makefile,v 1.15 2021/02/16 16:27:34 naddy Exp $

CFLAGS+=-DSMALL -DNOSSL

PROG=	ftp
SRCS=	fetch.c ftp.c main.c small.c util.c
.PATH:	${.CURDIR}/../../../usr.bin/ftp

.include <bsd.prog.mk>
