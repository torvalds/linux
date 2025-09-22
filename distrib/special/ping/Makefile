#	$OpenBSD: Makefile,v 1.3 2018/06/25 16:29:00 deraadt Exp $

PROG=	ping
COPTS+=	-DSMALL
.PATH:  ${.CURDIR}/../../../sbin/ping

LDADD=	-lm
DPADD=	${LIBM}

.include <bsd.prog.mk>
