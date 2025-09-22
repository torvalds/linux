#	$OpenBSD: Makefile,v 1.2 2023/08/14 08:56:20 kn Exp $

PROG=	bioctl
SRCS=	bioctl.c

LDADD=	-lutil
DPADD=	${LIBUTIL}

CDIAGFLAGS=     -Wall
CDIAGFLAGS+=    -Wpointer-arith
CDIAGFLAGS+=    -Wno-uninitialized
CDIAGFLAGS+=    -Wstrict-prototypes
CDIAGFLAGS+=    -Wmissing-prototypes
CDIAGFLAGS+=    -Wunused
CDIAGFLAGS+=    -Wsign-compare
CDIAGFLAGS+=    -Wshadow

.PATH:  ${.CURDIR}/../../../sbin/bioctl
.include <bsd.prog.mk>
