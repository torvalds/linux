# $FreeBSD$

.PATH: ${SRCTOP}/cddl/contrib/opensolaris/tools/ctf/common
.PATH: ${SRCTOP}/cddl/contrib/opensolaris/tools/ctf/dump

PROG=		ctfdump
SRCS=		dump.c \
		symbol.c \
		utils.c

CFLAGS+=	-I${OPENSOLARIS_USR_DISTDIR} \
		-I${OPENSOLARIS_SYS_DISTDIR} \
		-I${OPENSOLARIS_USR_DISTDIR}/head \
		-I${OPENSOLARIS_USR_DISTDIR}/cmd/mdb/tools/common \
		-I${SRCTOP}/sys/cddl/compat/opensolaris \
		-I${SRCTOP}/cddl/compat/opensolaris/include \
		-I${OPENSOLARIS_USR_DISTDIR}/tools/ctf/common \
		-I${OPENSOLARIS_SYS_DISTDIR}/uts/common

LIBADD=		elf z

.include <bsd.prog.mk>
