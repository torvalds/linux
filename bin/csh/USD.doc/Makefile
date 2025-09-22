#	$OpenBSD: Makefile,v 1.3 2004/02/01 15:15:40 jmc Exp $
#	$NetBSD: Makefile,v 1.3 1995/03/21 09:03:31 cgd Exp $

DIR=	usd/04.csh
SRCS=	tabs csh.1 csh.2 csh.3 csh.4 csh.a csh.g
MACROS= -ms

paper.txt: ${SRCS}
	${ROFF} -Tascii ${SRCS} > ${.TARGET}

.include <bsd.doc.mk>
