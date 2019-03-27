# $FreeBSD$

.PATH: ${SRCTOP}/cddl/contrib/opensolaris/common/ctf
.PATH: ${SRCTOP}/cddl/contrib/opensolaris/lib/libctf/common
.PATH: ${SRCTOP}/sys/cddl/contrib/opensolaris/common/ctf

LIB=		ctf
SRCS=		ctf_create.c \
		ctf_decl.c \
		ctf_error.c \
		ctf_hash.c \
		ctf_labels.c \
		ctf_lib.c \
		ctf_lookup.c \
		ctf_open.c \
		ctf_subr.c \
		ctf_types.c \
		ctf_util.c
MAN=		ctf.5

WARNS?=		2
CFLAGS+=	-DCTF_OLD_VERSIONS

CFLAGS+=	-I${SRCTOP}/sys/cddl/compat/opensolaris \
		-I${SRCTOP}/cddl/compat/opensolaris/include \
		-I${OPENSOLARIS_USR_DISTDIR}/head \
		-I${OPENSOLARIS_USR_DISTDIR}/common/ctf \
		-I${OPENSOLARIS_USR_DISTDIR}/lib/libctf/common \
		-I${OPENSOLARIS_SYS_DISTDIR}/uts/common

LIBADD+=	z

.include <bsd.lib.mk>
