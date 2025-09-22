#	$OpenBSD: Makefile,v 1.4 2023/08/14 08:56:20 kn Exp $

PROG=	fsck_ffs
SRCS=	dir.c inode.c main.c pass1.c pass1b.c pass2.c pass3.c pass4.c \
	pass5.c fsutil.c setup.c utilities.c ffs_subr.c ffs_tables.c
CFLAGS+= -I${.CURDIR}/../../../sbin/fsck -DSMALL
DPADD+=	${LIBUTIL}
LDADD+=	-lutil

.PATH:  ${.CURDIR}/../../../sbin/fsck_ffs
.PATH:	${.CURDIR}/../../../sbin/fsck
.PATH:	${.CURDIR}/../../../sys/ufs/ffs
.include <bsd.prog.mk>
