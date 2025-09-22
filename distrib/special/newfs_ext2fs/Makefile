# $OpenBSD: Makefile,v 1.2 2023/08/14 08:56:21 kn Exp $
PROG=	newfs_ext2fs
SRCS=	newfs_ext2fs.c mke2fs.c ext2fs_bswap.c

.PATH:	${.CURDIR}/../../../sys/ufs/ext2fs
DPADD+= ${LIBUTIL}
LDADD+= -lutil

.PATH:  ${.CURDIR}/../../../sbin/newfs_ext2fs
.include <bsd.prog.mk>
