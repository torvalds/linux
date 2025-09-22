#	$Id: Makefile,v 1.11 2023/08/14 08:56:21 kn Exp $

.if ${MACHINE} == "macppc"
PROG=	pdisk
LDADD=  -lutil
DPADD=  ${LIBUTIL}
CFLAGS+=-Wall

SRCS=	dump.c file_media.c io.c partition_map.c pdisk.c

.else
NOPROG=yes
.endif

.PATH:  ${.CURDIR}/../../../sbin/pdisk
.include <bsd.prog.mk>
