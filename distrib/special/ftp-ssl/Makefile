#	$OpenBSD: Makefile,v 1.3 2019/05/16 12:44:17 florian Exp $

CFLAGS+=-DSMALL

PROG=	ftp
SRCS=	fetch.c ftp.c main.c small.c util.c cookie.c
.PATH:	${.CURDIR}/../../../usr.bin/ftp

LDADD+=	-lutil -ltls -lssl -lcrypto
DPADD+=	${LIBUTIL} ${LIBTLS} ${LIBSSL} ${LIBCRYPTO}

.include <bsd.prog.mk>
