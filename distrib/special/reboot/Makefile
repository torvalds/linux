#	$OpenBSD: Makefile,v 1.3 2023/08/14 08:56:21 kn Exp $

PROG=	reboot
DPADD=	${LIBUTIL}
LDADD=	-lutil
LINKS=	${BINDIR}/reboot ${BINDIR}/halt

.PATH:  ${.CURDIR}/../../../sbin/reboot
.include <bsd.prog.mk>
