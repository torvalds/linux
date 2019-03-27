# $Id: Makefile 2289 2011-12-04 07:11:47Z jkoshy $

TOP=	..

PROG=	elfdump
WARNS?=	6

DPADD=	${LIBELFTC} ${LIBELF}
LDADD=	-lelftc -lelf

.include "${TOP}/mk/elftoolchain.prog.mk"
