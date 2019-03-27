# $FreeBSD$

.PATH: ${SRCTOP}/cddl/contrib/opensolaris/cmd/ztest

PROG=	ztest
MAN=

WARNS?=	0
CFLAGS+= -I${SRCTOP}/sys/cddl/compat/opensolaris
CFLAGS+= -I${SRCTOP}/cddl/compat/opensolaris/include
CFLAGS+= -I${SRCTOP}/cddl/compat/opensolaris/lib/libumem
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libzpool/common
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libnvpair
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libcmdutils
CFLAGS+= -I${SRCTOP}/sys/cddl/contrib/opensolaris/common/zfs
CFLAGS+= -I${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/fs/zfs
CFLAGS+= -I${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/head

LIBADD=	geom m nvpair umem zpool pthread avl zfs_core zfs uutil

CSTD=	c99

# Since there are many asserts in this program, it makes no sense to compile
# it without debugging.
CFLAGS+= -g -DDEBUG=1 -Wno-format
CFLAGS.gcc+= -fms-extensions

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.prog.mk>
