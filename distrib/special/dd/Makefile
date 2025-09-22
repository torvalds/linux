#	$OpenBSD: Makefile,v 1.3 2009/10/28 07:36:49 deraadt Exp $

# Define NO_CONV to disable character conversion
CFLAGS+=-DNO_CONV

PROG=	dd
SRCS=	args.c conv.c dd.c misc.c position.c

.PATH:	${.CURDIR}/../../../bin/dd

.include <bsd.prog.mk>
