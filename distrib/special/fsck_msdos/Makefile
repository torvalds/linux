#	$OpenBSD: Makefile,v 1.2 2023/08/14 08:56:20 kn Exp $

PROG=	fsck_msdos
SRCS=	main.c check.c boot.c fat.c dir.c fsutil.c
.PATH: ${.CURDIR}/../../../sbin/fsck
CFLAGS+= -I${.CURDIR}/../../../sbin/fsck
DPADD+=	${LIBUTIL}
LDADD+=	-lutil

.PATH:  ${.CURDIR}/../../../sbin/fsck_msdos
.include <bsd.prog.mk>
