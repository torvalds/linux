# $FreeBSD$

.PATH: ${SRCTOP}/cddl/compat/opensolaris/misc
.PATH: ${SRCTOP}/sys/cddl/contrib/opensolaris/common/zfs
.PATH: ${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/fs/zfs
.PATH: ${SRCTOP}/cddl/contrib/opensolaris/lib/libzfs/common
.PATH: ${SRCTOP}/cddl/contrib/opensolaris/lib/libcmdutils/common

LIB=	zfs
LIBADD=	md pthread umem util uutil m avl bsdxml geom nvpair z zfs_core
SRCS=	deviceid.c \
	fsshare.c \
	mkdirp.c \
	mnttab.c \
	thread_pool.c \
	zmount.c \
	zone.c

SRCS+=	nicenum.c

SRCS+=	libzfs_changelist.c \
	libzfs_compat.c \
	libzfs_config.c \
	libzfs_dataset.c \
	libzfs_diff.c \
	libzfs_import.c \
	libzfs_iter.c \
	libzfs_mount.c \
	libzfs_pool.c \
	libzfs_sendrecv.c \
	libzfs_status.c \
	libzfs_util.c \
	zfeature_common.c \
	zfs_comutil.c \
	zfs_deleg.c \
	zfs_fletcher.c \
	zfs_namecheck.c \
	zfs_prop.c \
	zpool_prop.c \
	zprop_common.c \

WARNS?=	0
SHLIB_MAJOR= 3
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
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libzfs/common
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libzfs_core/common
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libcmdutils

.include <bsd.lib.mk>
