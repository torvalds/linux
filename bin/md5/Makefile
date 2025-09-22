#	$OpenBSD: Makefile,v 1.15 2016/03/30 06:38:40 jmc Exp $

PROG=	md5
SRCS=	crc.c md5.c
MAN=	cksum.1 md5.1
LINKS=	${BINDIR}/md5 ${BINDIR}/sha1 \
	${BINDIR}/md5 ${BINDIR}/sha256 \
	${BINDIR}/md5 ${BINDIR}/sha512 \
	${BINDIR}/md5 ${BINDIR}/cksum

CPPFLAGS+= -I${.CURDIR}
COPTS+=	-Wall -Wconversion -Wmissing-prototypes

.include <bsd.prog.mk>
