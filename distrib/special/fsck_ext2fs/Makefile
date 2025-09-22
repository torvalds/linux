#	$OpenBSD: Makefile,v 1.4 2024/12/18 14:09:18 sthen Exp $

PROG=	fsck_ext2fs
SRCS=	dir.c inode.c main.c pass1.c pass1b.c pass2.c pass3.c pass4.c \
	pass5.c fsutil.c setup.c utilities.c ext2fs_bswap.c
CFLAGS+= -I${.CURDIR}/../../../sbin/fsck -DSMALL
DPADD+=	${LIBUTIL}
LDADD+=	-lutil

.PATH:	${.CURDIR}/../../../sbin/fsck_ext2fs
.PATH:	${.CURDIR}/../../../sbin/fsck
.PATH:	${.CURDIR}/../../../sys/ufs/ext2fs
.include <bsd.prog.mk>
