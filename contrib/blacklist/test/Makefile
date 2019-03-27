# $NetBSD: Makefile,v 1.3 2015/05/30 22:40:38 christos Exp $

MKMAN=no

PROGS=srvtest cltest 
SRCS.srvtest = srvtest.c
SRCS.cltest = cltest.c
CPPFLAGS+=-DBLDEBUG
LDADD+=-lutil
DPADD+=${LIBUTIL}

.include <bsd.prog.mk>
