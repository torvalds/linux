#	$OpenBSD: Makefile,v 1.2 2023/08/14 08:56:21 kn Exp $

PROG=	umount

CFLAGS+= -DNO_NFS
DPADD+=	${LIBUTIL}
LDADD+=	-lutil

.PATH:	${.CURDIR}/../../../sbin/umount
.include <bsd.prog.mk>
