#	$OpenBSD: Makefile,v 1.3 2020/07/21 13:57:08 deraadt Exp $

.PATH: ${.CURDIR}/../../../usr.sbin/eeprom

.if ${MACHINE} == "arm64" || ${MACHINE} == "armv7" || \
    ${MACHINE} == "macppc" || ${MACHINE} == "octeon" || \
    ${MACHINE} == "powerpc64" || ${MACHINE} == "sparc64"
PROG=	eeprom

SRCS=	main.c
SRCS+=	ophandlers.c optree.c

CFLAGS+=	-DSMALL
.else
NOPROG=yes
.endif

.include <bsd.prog.mk>
