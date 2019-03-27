# $Id: Makefile 2076 2011-10-27 03:50:33Z jkoshy $

TOP=	..

PROG=	readelf
SRCS=	readelf.c

WARNS?=	6

DPADD=	${LIBDWARF} ${LIBELF}
LDADD=	-ldwarf -lelftc -lelf

MAN1=	readelf.1

.include "${TOP}/mk/elftoolchain.prog.mk"
