#	$OpenBSD: Makefile,v 1.10 2024/06/30 18:17:07 deraadt Exp $

PROG=	gzip
SRCS=	main.c gzopen.c
SRCS+=	adler32.c crc32.c inflate.c inftrees.c zutil.c
COPTS+=	-DSMALL -DDYNAMIC_CRC_TABLE -DNO_GZIP -DSLOW
.PATH:  ${.CURDIR}/../../../usr.bin/compress
.PATH:  ${.CURDIR}/../../../lib/libz

.include <bsd.prog.mk>
