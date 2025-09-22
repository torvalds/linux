#	$OpenBSD: Makefile,v 1.2 2023/08/14 08:56:21 kn Exp $

PROG=	mount_nfs
SRCS=	mount_nfs.c getmntopts.c

MOUNT=	${.CURDIR}/../../../sbin/mount
CFLAGS+= -DNFS -I${MOUNT}
.PATH:	${MOUNT}

.PATH:  ${.CURDIR}/../../../sbin/mount_nfs
.include <bsd.prog.mk>
