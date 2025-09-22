#	$OpenBSD: Makefile,v 1.2 2023/08/14 08:56:21 kn Exp $

PROG=	mount_udf
SRCS=	mount_udf.c getmntopts.c

MOUNT=	${.CURDIR}/../../../sbin/mount
CFLAGS+= -I${MOUNT}
.PATH:	${MOUNT}

.PATH:  ${.CURDIR}/../../../sbin/mount_udf
.include <bsd.prog.mk>
