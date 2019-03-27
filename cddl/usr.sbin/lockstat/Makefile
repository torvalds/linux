# $FreeBSD$

.PATH: ${SRCTOP}/cddl/contrib/opensolaris/cmd/lockstat

PROG=		lockstat
SRCS=		lockstat.c sym.c
BINDIR?=	/usr/sbin

WARNS?=		1

CFLAGS+=	-I${SRCTOP}/sys/cddl/compat/opensolaris \
		-I${SRCTOP}/cddl/compat/opensolaris/include \
		-I${OPENSOLARIS_USR_DISTDIR}/head \
		-I${OPENSOLARIS_USR_DISTDIR}/lib/libdtrace/common \
		-I${OPENSOLARIS_USR_DISTDIR}/lib/libproc/common \
		-I${OPENSOLARIS_SYS_DISTDIR}/uts/common \
		-I${OPENSOLARIS_SYS_DISTDIR}/compat \
		-I${SRCTOP}/sys

CFLAGS+=	-DNEED_ERRLOC -g

#YFLAGS+=	-d

LIBADD=	elf dtrace rt

.include <bsd.prog.mk>
