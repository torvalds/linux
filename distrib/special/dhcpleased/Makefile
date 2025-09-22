#	$OpenBSD: Makefile,v 1.3 2023/08/14 08:56:20 kn Exp $

PROG=	dhcpleased
COPTS+=	-DSMALL
.PATH:  ${.CURDIR}/../../../sbin/dhcpleased
SRCS=	bpf.c checksum.c dhcpleased.c engine.c frontend.c

LDADD+=	-levent -lutil
DPADD+= ${LIBEVENT} ${LIBUTIL}

.include <bsd.prog.mk>
