#	$OpenBSD: Makefile,v 1.1 2014/02/12 21:39:34 miod Exp $

.PATH: ${.CURDIR}/../../../usr.sbin/mkuboot

.if ${MACHINE} == "armv7"

BINDIR=	/usr/sbin
PROG=	mkuboot
SRCS=	mkuboot.c copy_elf32.c copy_elf64.c
DPADD=	${LIBZ}
LDADD=	-lz
CFLAGS=	-DMACHINE_ARCH=\"${MACHINE_ARCH}\"

.else

NOPROG=yes

.endif

.include <bsd.prog.mk>
