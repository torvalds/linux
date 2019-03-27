# $Id: Makefile 2066 2011-10-26 15:40:28Z jkoshy $

TOP=	..

PROG=	c++filt
SRCS=	cxxfilt.c

WARNS?=	6

DPADD=	${LIBELFTC} ${LIBELF}
LDADD=	-lelftc -lelf

MAN1=	c++filt.1

.include "${TOP}/mk/elftoolchain.prog.mk"
