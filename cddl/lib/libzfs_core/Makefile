# $FreeBSD$

.PATH: ${SRCTOP}/cddl/compat/opensolaris/misc
.PATH: ${SRCTOP}/sys/cddl/contrib/opensolaris/common/zfs
.PATH: ${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/fs/zfs
.PATH: ${SRCTOP}/cddl/contrib/opensolaris/lib/libzfs_core/common
.PATH: ${SRCTOP}/cddl/contrib/opensolaris/lib/libzfs/common

LIB=	zfs_core
LIBADD=	nvpair

INCS=	libzfs_core.h
SRCS=	libzfs_core.c \
	libzfs_core_compat.c \
	zfs_ioctl_compat.c

SRCS+=	libzfs_compat.c

WARNS?=	0
CSTD=	c99
CFLAGS+= -DZFS_NO_ACL
CFLAGS+= -I${SRCTOP}/sbin/mount
CFLAGS+= -I${SRCTOP}/sys/cddl/compat/opensolaris
CFLAGS+= -I${SRCTOP}/cddl/compat/opensolaris/include
CFLAGS+= -I${SRCTOP}/cddl/compat/opensolaris/lib/libumem
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libzpool/common
CFLAGS+= -I${SRCTOP}/sys/cddl/contrib/opensolaris/common/zfs
CFLAGS+= -I${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/fs/zfs
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/head
CFLAGS+= -I${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libnvpair
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libuutil/common
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libzfs_core/common
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libzfs/common

.include <bsd.lib.mk>
