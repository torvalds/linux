#	$OpenBSD: Makefile,v 1.2 2004/02/01 15:21:55 jmc Exp $

DIR=	usd/10.edadv
SRCS=	ae.mac ae0 ae1 ae2 ae3 ae4 ae5 ae6 ae7 ae9
MACROS= -ms

paper.txt: ${SRCS}
	${ROFF} -Tascii ${SRCS} > ${.TARGET}

.include <bsd.doc.mk>
