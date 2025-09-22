#	$OpenBSD: Makefile,v 1.4 2010/07/02 20:52:10 nicm Exp $

PROG=	grep
SRCS=	binary.c file.c grep.c mmfile.c queue.c util.c
COPTS+=	-DNOZ -DSMALL
.PATH:  ${.CURDIR}/../../../usr.bin/grep

.include <bsd.prog.mk>
