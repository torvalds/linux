# $Id: Makefile 3608 2018-04-14 21:23:04Z jkoshy $

TOP=	..

PROG=	ar
SRCS=	ar.c read.c util.c write.c
LSRC=	acplex.l
YSRC=	acpyacc.y

WARNS?=	5

DPADD=	${LIBARCHIVE} ${LIBELFTC} ${LIBELF} ${LIBZ}
LDADD=	-larchive -lelftc -lelf -lz

CFLAGS+=-I. -I${.CURDIR}

LINKS=	${BINDIR}/ar ${BINDIR}/ranlib

EXTRA_TARGETS=	ranlib

CLEANFILES+= 	${EXTRA_TARGETS}

MAN=	ar.1 ranlib.1 ar.5

all:	${EXTRA_TARGETS}

${EXTRA_TARGETS}:	${PROG}
	ln -s ${PROG} ${.TARGET}

.include "${TOP}/mk/elftoolchain.prog.mk"

.if ${OS_HOST} == "OpenBSD"
CFLAGS+=	-I/usr/local/include
LDFLAGS+=	-L/usr/local/lib
.elif ${OS_HOST} == "DragonFly"
LDADD+=	-lbz2
.endif
