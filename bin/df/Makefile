#	@(#)Makefile	8.3 (Berkeley) 5/8/95
# $FreeBSD$

PACKAGE=runtime
MOUNT=	${SRCTOP}/sbin/mount
.PATH: ${MOUNT}

PROG=	df
SRCS=	df.c vfslist.c

CFLAGS+= -I${MOUNT}

CFLAGS+= -DMOUNT_CHAR_DEVS
SRCS+=	getmntopts.c

LIBADD=	xo util

.include <bsd.prog.mk>
