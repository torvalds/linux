#	@(#)Makefile.inc	8.1 (Berkeley) 5/31/93
# $FreeBSD$

.include <src.opts.mk>

BINDIR?=	/bin
WARNS?=		6

.if ${MK_DYNAMICROOT} == "no"
NO_SHARED?=	YES
.endif
