#	$OpenBSD: Makefile,v 1.3 2009/10/28 07:36:49 deraadt Exp $

PROG=	init
CFLAGS+=-DDEFAULT_STATE=single_user
DPADD=	${LIBUTIL}
LDADD=	-lutil
.PATH:  ${.CURDIR}/../../../sbin/init

.include <bsd.prog.mk>
