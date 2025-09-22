#	$OpenBSD: Makefile,v 1.1 2014/12/23 17:16:02 deraadt Exp $

PROG=	df
SRCS=	df.c ffs_df.c ext2fs_df.c
LDADD=	-lutil
DPADD=	${LIBUTIL}

.PATH:	${.CURDIR}/../../../bin/df
.include <bsd.prog.mk>
